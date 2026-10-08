#include "ble_service.h"

#include "ble_protocol.h"
#include "measure.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui_screens.h"

#if CONFIG_BT_NIMBLE_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "os/os_mbuf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"

/* "Porozit Watch 1A2B": the last two MAC bytes tell units apart (PROTOCOL.md) */
#define BLE_DEVICE_NAME_PREFIX "Porozit Watch"
/* Longest value this service reads or notifies. The preferred ATT MTU is 256
 * (CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU), and a notification can carry MTU - 3
 * bytes, so 244 stays inside a single packet while leaving room for a smaller
 * negotiated MTU. It used to be 128, which silently dropped the status reply
 * once that grew past it. */
#define BLE_MAX_VALUE_LEN 244

static const char *TAG = "ble";

static uint8_t ble_addr_type;
static uint16_t ble_gatt_char_handle;
static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool ble_notify_enabled;
/* What a READ of the characteristic returns: always the most recent outgoing
 * message. The phone's own writes used to be stored here, so an app that writes
 * a request and then reads the answer got its own request echoed back. */
static uint8_t ble_value[BLE_MAX_VALUE_LEN];
static size_t ble_value_len;
/* Last payload written by the phone, deliberately separate from ble_value */
static uint8_t ble_rx_buf[BLE_MAX_VALUE_LEN];
static bool ble_initialized;
static bool ble_low_power;
static void ble_app_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        int rc = os_mbuf_append(ctxt->om, ble_value, ble_value_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        /* om_len is only the first buffer of the chain, the total length of a
         * long write is OS_MBUF_PKTLEN(). */
        const uint16_t total_len = OS_MBUF_PKTLEN(ctxt->om);

        if (total_len > BLE_MAX_VALUE_LEN) {
            ESP_LOGW(TAG, "Write of %u bytes exceeds the %d byte buffer",
                     (unsigned)total_len, BLE_MAX_VALUE_LEN);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        if (os_mbuf_copydata(ctxt->om, 0, total_len, ble_rx_buf) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        ESP_LOGI(TAG, "Write %u bytes", (unsigned)total_len);

        /* This callback runs in the NimBLE host task, which must not block on
         * NVS writes or the LVGL lock, so the payload is only handed over. */
        ble_protocol_receive(ble_rx_buf, total_len);
        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
            .uuid = BLE_UUID16_DECLARE(0xFFF1),
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &ble_gatt_char_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;

    if (ctxt->op == BLE_GATT_REGISTER_OP_CHR) {
        ESP_LOGI(TAG, "GATT chr registered: handle=%u",
                 (unsigned)ctxt->chr.val_handle);
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "Reset; reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &ble_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    ble_app_advertise();
}

static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    if (ble_low_power) {
        ESP_LOGI(TAG, "Advertising suppressed (low power)");
        return;
    }

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    /* Lets the app recognise a Porozit by its service, not only by name */
    static const ble_uuid16_t service_uuid = BLE_UUID16_INIT(0xFFF0);
    fields.uuids16 = &service_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising started");
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ble_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, ATT MTU %u", (unsigned)ble_att_mtu(ble_conn_handle));
            ui_set_bluetooth_connected(true);
            measure_refresh_save_state();
        } else {
            ESP_LOGW(TAG, "Connect failed; status=%d", event->connect.status);
            ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_app_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_notify_enabled = false;
        ui_set_bluetooth_connected(false);
        measure_refresh_save_state();
        ble_app_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete; reason=%d",
                 event->adv_complete.reason);
        ble_app_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == ble_gatt_char_handle) {
            ble_notify_enabled = event->subscribe.cur_notify;
            /* Saving needs a subscribed phone, so the button follows this */
            measure_refresh_save_state();
        }
        ESP_LOGI(TAG, "Subscribe: attr=%u notify=%d indicate=%d",
                 (unsigned)event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated; conn=%u mtu=%u",
                 (unsigned)event->mtu.conn_handle,
                 (unsigned)event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void ble_host_task(void *param)
{
    (void)param;

    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_service_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(nimble_port_init());

    ble_svc_gap_init();
    ble_svc_gatt_init();

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_BT);
    static char device_name[24];
    snprintf(device_name, sizeof(device_name), "%s %02X%02X", BLE_DEVICE_NAME_PREFIX, mac[4], mac[5]);
    ble_svc_gap_device_name_set(device_name);

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    ble_initialized = true;
    return ESP_OK;
}

void ble_service_set_low_power(bool low_power)
{
    if (ble_low_power == low_power) {
        return;
    }

    ble_low_power = low_power;

    if (!ble_initialized) {
        return;
    }

    if (low_power) {
        /* Keep an existing link, just stop burning power on advertising */
        int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "ble_gap_adv_stop failed: %d", rc);
        } else {
            ESP_LOGI(TAG, "Advertising stopped (low power)");
        }
    } else if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ble_app_advertise();
    }
}

bool ble_service_can_notify(void)
{
    return ble_conn_handle != BLE_HS_CONN_HANDLE_NONE && ble_notify_enabled;
}

esp_err_t ble_service_notify(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > BLE_MAX_VALUE_LEN) {
        /* Loud on purpose: a too long notification used to be dropped here
         * without a trace, which looks exactly like the phone never replying. */
        ESP_LOGE(TAG, "Notification of %u bytes exceeds the %d byte limit, dropped",
                 (unsigned)len, BLE_MAX_VALUE_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Publish it as the readable value before anything can fail, so a phone
     * that reads the characteristic instead of subscribing still gets the
     * answer rather than its own request. */
    memcpy(ble_value, data, len);
    ble_value_len = len;

    if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Nobody connected, %u bytes stored but not notified",
                 (unsigned)len);
        return ESP_ERR_INVALID_STATE;
    }

    if (!ble_notify_enabled) {
        /* The phone has not written the CCCD. Without that NimBLE refuses to
         * notify, which looks exactly like the watch never answering. */
        ESP_LOGW(TAG, "Notifications are not subscribed, %u bytes are readable "
                      "but were not pushed", (unsigned)len);
        return ESP_ERR_INVALID_STATE;
    }

    /* A notification carries at most MTU - 3 bytes. Anything longer is cut off
     * by the stack and arrives as invalid JSON, so refuse it loudly instead. */
    const uint16_t mtu = ble_att_mtu(ble_conn_handle);
    if (mtu != 0 && (len + 3) > mtu) {
        ESP_LOGE(TAG, "%u byte notification does not fit the negotiated ATT MTU "
                      "of %u, the phone would get a truncated payload",
                 (unsigned)len, (unsigned)mtu);
        return ESP_ERR_INVALID_SIZE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(ble_conn_handle, ble_gatt_char_handle, om);
    if (rc == 0) {
        ESP_LOGI(TAG, "Notified %u bytes (MTU %u)", (unsigned)len, (unsigned)mtu);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Notify failed: %d", rc);
    return ESP_FAIL;
}

#else

esp_err_t ble_service_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void ble_service_set_low_power(bool low_power)
{
    (void)low_power;
}

bool ble_service_can_notify(void)
{
    return false;
}

#endif
