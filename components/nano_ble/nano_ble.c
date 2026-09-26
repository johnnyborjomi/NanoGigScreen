#include "nano_ble.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "nano_ble";

#define UUID_SERVICE_A002 0xa002
#define UUID_CHAR_C304 0xc304
#define UUID_CHAR_C305 0xc305
#define UUID_CCCD 0x2902
#define PREFERRED_MTU 517
#define CONNECT_TIMEOUT_MS 30000

static nano_ble_callbacks_t s_cb;
static volatile nano_ble_status_t s_status = NANO_BLE_IDLE;
static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_c304_handle, s_c305_handle, s_c305_def_handle, s_c305_cccd;
static uint16_t s_c305_end; /* last handle that can hold a c305 descriptor */
static uint16_t s_mtu;
static volatile bool s_enabled = true;

static void set_status(nano_ble_status_t st, const char *detail)
{
    s_status = st;
    if (s_cb.on_status) s_cb.on_status(st, detail);
}

/* The pedal uses Bluetooth-base UUIDs (0000xxxx-0000-1000-8000-00805f9b34fb); accept both encodings. */
static bool uuid_is_short(const ble_uuid_t *u, uint16_t want)
{
    if (u->type == BLE_UUID_TYPE_16) return ble_uuid_u16(u) == want;
    if (u->type == BLE_UUID_TYPE_128) {
        static const uint8_t base[16] = { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0, 0, 0x00, 0x00 };
        const uint8_t *v = ((const ble_uuid128_t *)u)->value;
        return memcmp(v, base, 12) == 0 && v[12] == (want & 0xff) && v[13] == (want >> 8) && v[14] == 0 && v[15] == 0;
    }
    return false;
}

static int gap_event(struct ble_gap_event *event, void *arg);

static void start_scan(void)
{
    if (!s_enabled) {
        set_status(NANO_BLE_IDLE, "Disconnected");
        return;
    }
    struct ble_gap_disc_params p = {
        .passive = 0, /* active: the name may only be in the scan response */
        .filter_duplicates = 1,
        .itvl = 0,
        .window = 0,
    };
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
        set_status(NANO_BLE_IDLE, "scan failed");
        return;
    }
    set_status(NANO_BLE_SCANNING, "Looking for the pedal");
}

static bool looks_like_nano(const struct ble_hs_adv_fields *f)
{
    if (f->name && f->name_len) {
        char name[32] = { 0 };
        size_t n = f->name_len < sizeof(name) - 1 ? f->name_len : sizeof(name) - 1;
        memcpy(name, f->name, n);
        for (size_t i = 0; i < n; i++) name[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
        if (strstr(name, "nano") || strstr(name, "cortex") || strstr(name, "neural")) return true;
    }
    for (int i = 0; i < f->num_uuids16; i++) {
        if (ble_uuid_u16(&f->uuids16[i].u) == UUID_SERVICE_A002) return true;
    }
    return false;
}

static void reset_link(void)
{
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_svc_start = s_svc_end = 0;
    s_c304_handle = s_c305_handle = s_c305_def_handle = s_c305_cccd = 0;
    s_c305_end = 0;
    s_mtu = 0;
}

/* ---- discovery chain: service -> characteristics -> c305 descriptors -> CCCD write ------ */

static int on_cccd_written(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)attr; (void)arg;
    if (error->status != 0) {
        ESP_LOGE(TAG, "subscribe c305 failed: %d", error->status);
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }
    ESP_LOGI(TAG, "c305 subscribed, c304 handle %u, MTU %u", s_c304_handle, s_mtu);
    set_status(NANO_BLE_READY, "Connected");
    return 0;
}

static int on_dsc(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn_handle; (void)chr_val_handle; (void)arg;
    if (error->status == 0 && dsc) {
        /* Only the first CCCD: c306's follows right after and must stay off. */
        if (!s_c305_cccd && uuid_is_short(&dsc->uuid.u, UUID_CCCD)) s_c305_cccd = dsc->handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_c305_cccd) {
            ESP_LOGE(TAG, "c305 has no CCCD");
            ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        uint8_t notify_on[2] = { 0x01, 0x00 }; /* notify only; never enable indications (c306 mirror lag) */
        int rc = ble_gattc_write_flat(s_conn_handle, s_c305_cccd, notify_on, sizeof(notify_on), on_cccd_written, NULL);
        if (rc) ESP_LOGE(TAG, "cccd write rc=%d", rc);
        return 0;
    }
    if (error->status != 0) {
        ESP_LOGE(TAG, "descriptor discovery failed: %d", error->status);
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

static int on_chr(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle; (void)arg;
    if (error->status == 0 && chr) {
        if (uuid_is_short(&chr->uuid.u, UUID_CHAR_C304)) s_c304_handle = chr->val_handle;
        if (uuid_is_short(&chr->uuid.u, UUID_CHAR_C305)) {
            s_c305_handle = chr->val_handle;
            s_c305_def_handle = chr->def_handle;
        } else if (s_c305_handle && !s_c305_end && chr->def_handle > s_c305_handle) {
            s_c305_end = chr->def_handle - 1; /* the next characteristic's declaration ends c305's descriptors */
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_c304_handle || !s_c305_handle) {
            ESP_LOGE(TAG, "c304/c305 not found (c304=%u c305=%u)", s_c304_handle, s_c305_handle);
            ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        /* Descriptors of c305 live between its value handle and the next characteristic / service end. */
        uint16_t end = s_c305_end ? s_c305_end : s_svc_end;
        int rc = ble_gattc_disc_all_dscs(s_conn_handle, s_c305_handle, end, on_dsc, NULL);
        if (rc) ESP_LOGE(TAG, "disc dscs rc=%d", rc);
        return 0;
    }
    ESP_LOGE(TAG, "characteristic discovery failed: %d", error->status);
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int on_svc(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn_handle; (void)arg;
    if (error->status == 0 && svc) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_svc_start) {
            ESP_LOGE(TAG, "service a002 not found");
            ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        int rc = ble_gattc_disc_all_chrs(s_conn_handle, s_svc_start, s_svc_end, on_chr, NULL);
        if (rc) ESP_LOGE(TAG, "disc chrs rc=%d", rc);
        return 0;
    }
    ESP_LOGE(TAG, "service discovery failed: %d", error->status);
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int on_mtu(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    (void)conn_handle; (void)arg;
    if (error->status == 0) {
        s_mtu = mtu;
        ESP_LOGI(TAG, "MTU %u", mtu);
        if (mtu < 515) ESP_LOGW(TAG, "MTU below 515: 512-byte dump packets will be truncated");
    } else {
        ESP_LOGW(TAG, "MTU exchange failed: %d", error->status);
    }
    ble_uuid16_t svc = BLE_UUID16_INIT(UUID_SERVICE_A002);
    int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &svc.u, on_svc, NULL);
    if (rc) ESP_LOGE(TAG, "disc svc rc=%d", rc);
    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields f;
        if (ble_hs_adv_parse_fields(&f, event->disc.data, event->disc.length_data) != 0) return 0;
        if (!looks_like_nano(&f)) return 0;
        ESP_LOGI(TAG, "pedal found, connecting");
        ble_gap_disc_cancel();
        set_status(NANO_BLE_CONNECTING, "Pedal found");
        int rc = ble_gap_connect(s_own_addr_type, &event->disc.addr, CONNECT_TIMEOUT_MS, NULL, gap_event, NULL);
        if (rc) {
            ESP_LOGE(TAG, "ble_gap_connect rc=%d", rc);
            start_scan();
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "connect failed: %d", event->connect.status);
            reset_link();
            start_scan();
            return 0;
        }
        s_conn_handle = event->connect.conn_handle;
        set_status(NANO_BLE_CONNECTING, "Connected, setting up");
        /* 517-byte MTU first: the pedal sends 512-byte notifications. */
        ble_gattc_exchange_mtu(s_conn_handle, on_mtu, NULL);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected: reason %d", event->disconnect.reason);
        reset_link();
        if (s_enabled) set_status(NANO_BLE_SCANNING, "Link lost");
        start_scan();
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE && s_status == NANO_BLE_SCANNING) start_scan();
        return 0;
    case BLE_GAP_EVENT_MTU:
        s_mtu = event->mtu.value;
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.attr_handle != s_c305_handle) return 0;
        if (event->notify_rx.indication) return 0; /* never expected: we only enable notify */
        uint8_t buf[PREFERRED_MTU];
        uint16_t len = 0;
        if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len) != 0) return 0;
        if (s_cb.on_notify) s_cb.on_notify(buf, len);
        return 0;
    }
    default:
        return 0;
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc) ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc) ESP_LOGE(TAG, "infer_auto rc=%d", rc);
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset, reason %d", reason);
    reset_link();
    set_status(NANO_BLE_IDLE, "Bluetooth reset");
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int nano_ble_start(const nano_ble_callbacks_t *cb)
{
    s_cb = *cb;
    reset_link();
    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init rc=%d", rc);
        return rc;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sm_bonding = 0; /* the pedal needs no pairing */
    ble_att_set_preferred_mtu(PREFERRED_MTU);
    ble_svc_gap_device_name_set("NanoGig Screen");
    nimble_port_freertos_init(host_task);
    return 0;
}

nano_ble_status_t nano_ble_status(void)
{
    return s_status;
}

uint16_t nano_ble_mtu(void)
{
    return s_mtu;
}

void nano_ble_set_enabled(bool enabled)
{
    if (s_enabled == enabled) return;
    s_enabled = enabled;
    if (enabled) {
        start_scan();
        return;
    }
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM); /* DISCONNECT event follows */
    } else {
        ble_gap_disc_cancel();
        set_status(NANO_BLE_IDLE, "Disconnected");
    }
}

bool nano_ble_enabled(void)
{
    return s_enabled;
}

int nano_ble_write(const uint8_t *data, size_t len)
{
    if (s_status != NANO_BLE_READY || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return BLE_HS_ENOTCONN;
    int rc = ble_gattc_write_flat(s_conn_handle, s_c304_handle, data, (uint16_t)len, NULL, NULL);
    if (rc) ESP_LOGW(TAG, "c304 write rc=%d", rc);
    return rc;
}
