#include "nano_ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "nano_ota";

#define NVS_NAMESPACE "nanogig_wifi"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define CONNECT_TIMEOUT_MS 20000
#define CONNECT_RETRIES 3
#define TASK_STACK 8192         /* TLS handshake + HTTP client */
#define RESTART_DELAY_MS 2000   /* long enough to read "Installed" */

#define BIT_CONNECTED BIT0
#define BIT_FAILED BIT1

typedef enum { CMD_SCAN, CMD_JOIN, CMD_CHECK, CMD_INSTALL } cmd_kind_t;
typedef struct {
    cmd_kind_t kind;
    char ssid[33];
    char pass[65];
} cmd_t;

static nano_ota_cb_t s_cb;
static QueueHandle_t s_cmds;
static EventGroupHandle_t s_bits;
static bool s_started, s_connected, s_want_link;
static int s_retries;
static uint8_t s_disconnect_reason;
static char s_ssid[33];                  /* the network we are on / joining */
static nano_ota_network_t s_networks[NANO_OTA_MAX_NETWORKS];
static int s_network_count;

/* ---- events ---------------------------------------------------------------- */

static void emit(nano_ota_event_kind_t kind, const char *text, int percent)
{
    if (!s_cb) return;
    nano_ota_event_t ev = { .kind = kind, .percent = percent };
    if (text) strlcpy(ev.text, text, sizeof(ev.text));
    if (kind == NANO_OTA_EV_SCAN_DONE) {
        ev.networks = s_networks;
        ev.network_count = s_network_count;
    }
    s_cb(&ev);
}

/* ---- version / rollback ------------------------------------------------------ */

const char *nano_ota_running_version(void)
{
    return esp_app_get_description()->version;
}

void nano_ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "new image %s reached the gig screen: keeping it", nano_ota_running_version());
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

/* ---- saved network --------------------------------------------------------- */

static bool creds_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = ssid_cap;
    bool ok = nvs_get_str(h, NVS_KEY_SSID, ssid, &n) == ESP_OK && ssid[0];
    if (ok && pass) {
        n = pass_cap;
        if (nvs_get_str(h, NVS_KEY_PASS, pass, &n) != ESP_OK) pass[0] = '\0';
    }
    nvs_close(h);
    return ok;
}

static void creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, NVS_KEY_SSID, ssid) == ESP_OK && nvs_set_str(h, NVS_KEY_PASS, pass) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

bool nano_ota_saved_ssid(char *out, size_t cap)
{
    return creds_load(out, cap, NULL, 0);
}

/* ---- Wi-Fi ----------------------------------------------------------------- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        s_connected = false;
        s_disconnect_reason = d->reason;
        if (!s_want_link) return;
        if (++s_retries <= CONNECT_RETRIES) {
            ESP_LOGW(TAG, "disconnected (reason %u), retry %d", d->reason, s_retries);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_bits, BIT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_connected = true;
        s_retries = 0;
        xEventGroupSetBits(s_bits, BIT_CONNECTED);
    }
}

static const char *reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "Wrong Wi-Fi password?";
    case WIFI_REASON_NO_AP_FOUND:
        return "Wi-Fi network not found";
    default:
        return "Could not join Wi-Fi";
    }
}

static bool wifi_connect(const char *ssid, const char *pass)
{
    if (s_connected && strcmp(s_ssid, ssid) == 0) return true;
    emit(NANO_OTA_EV_CONNECTING, ssid, 0);
    if (s_connected) {
        s_want_link = false;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN; /* strongest access point of a mesh, not the first one heard */
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    xEventGroupClearBits(s_bits, BIT_CONNECTED | BIT_FAILED);
    s_retries = 0;
    s_disconnect_reason = 0;
    s_want_link = true;
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(s_bits, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (bits & BIT_CONNECTED) return true;
    s_want_link = false;
    esp_wifi_disconnect();
    ESP_LOGW(TAG, "join \"%s\" failed (reason %u)", ssid, s_disconnect_reason);
    emit(NANO_OTA_EV_ERROR, s_disconnect_reason ? reason_text(s_disconnect_reason) : "Wi-Fi timed out", 0);
    return false;
}

static void wifi_scan(void)
{
    emit(NANO_OTA_EV_SCANNING, NULL, 0);
    if (!s_connected) {
        /* A failed join leaves the driver retrying in the background; a scan would be refused. */
        s_want_link = false;
        esp_wifi_disconnect();
    }
    s_network_count = 0;
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
        emit(NANO_OTA_EV_SCAN_DONE, NULL, 0);
        return;
    }
    uint16_t n = 24;
    wifi_ap_record_t *recs = calloc(n, sizeof(*recs)); /* ~2 KB: heap, only while scanning */
    if (!recs || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        n = 0;
        esp_wifi_clear_ap_list(); /* the driver keeps its records until they are read or cleared */
    }
    /* Records come strongest first: keep the first sighting of every named network. */
    for (int i = 0; i < n && s_network_count < NANO_OTA_MAX_NETWORKS; i++) {
        const char *name = (const char *)recs[i].ssid;
        if (!name[0]) continue;
        bool dup = false;
        for (int k = 0; k < s_network_count && !dup; k++) dup = strcmp(s_networks[k].ssid, name) == 0;
        if (dup) continue;
        nano_ota_network_t *net = &s_networks[s_network_count++];
        strlcpy(net->ssid, name, sizeof(net->ssid));
        net->rssi = recs[i].rssi;
        net->secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    free(recs);
    ESP_LOGI(TAG, "scan: %u records, %d networks", n, s_network_count);
    emit(NANO_OTA_EV_SCAN_DONE, NULL, 0);
}

/* ---- image ----------------------------------------------------------------- */

static esp_err_t image_open(esp_https_ota_handle_t *h, esp_app_desc_t *desc)
{
    esp_http_client_config_t http = {
        .url = CONFIG_NANOGIG_OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,    /* GitHub's redirect answer carries long headers */
        .buffer_size_tx = 3072, /* ... and points at a long signed asset URL */
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };
    *h = NULL;
    esp_err_t err = esp_https_ota_begin(&cfg, h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: %s", CONFIG_NANOGIG_OTA_URL, esp_err_to_name(err));
        *h = NULL;
        return err;
    }
    err = esp_https_ota_get_img_desc(*h, desc);
    if (err == ESP_OK && strcmp(desc->project_name, esp_app_get_description()->project_name) != 0) {
        ESP_LOGW(TAG, "image is \"%s\", not this project", desc->project_name);
        err = ESP_ERR_INVALID_VERSION;
    }
    if (err != ESP_OK) {
        esp_https_ota_abort(*h);
        *h = NULL;
    }
    return err;
}

static bool ensure_link(void)
{
    if (s_connected) return true;
    char ssid[33], pass[65];
    if (!creds_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        emit(NANO_OTA_EV_NO_WIFI, NULL, 0);
        return false;
    }
    return wifi_connect(ssid, pass);
}

static void check(void)
{
    if (!ensure_link()) return;
    emit(NANO_OTA_EV_CHECKING, NULL, 0);
    esp_https_ota_handle_t h;
    esp_app_desc_t desc;
    esp_err_t err = image_open(&h, &desc);
    if (err != ESP_OK) {
        emit(NANO_OTA_EV_ERROR, err == ESP_ERR_INVALID_VERSION ? "Wrong firmware at the update URL" : "Update server unreachable", 0);
        return;
    }
    esp_https_ota_abort(h); /* header only: nothing was written */
    const char *running = nano_ota_running_version();
    ESP_LOGI(TAG, "running %s, published %s", running, desc.version);
    emit(strcmp(desc.version, running) == 0 ? NANO_OTA_EV_UP_TO_DATE : NANO_OTA_EV_AVAILABLE, desc.version, 0);
}

static void install(void)
{
    if (!ensure_link()) return;
    emit(NANO_OTA_EV_PROGRESS, NULL, 0);
    esp_https_ota_handle_t h;
    esp_app_desc_t desc;
    esp_err_t err = image_open(&h, &desc);
    if (err != ESP_OK) {
        emit(NANO_OTA_EV_ERROR, "Update server unreachable", 0);
        return;
    }
    ESP_LOGI(TAG, "installing %s", desc.version);
    int total = esp_https_ota_get_image_size(h), last = -1;
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int read = esp_https_ota_get_image_len_read(h);
        int pct = total > 0 ? (int)((int64_t)read * 100 / total) : 0;
        if (pct != last) {
            last = pct;
            emit(NANO_OTA_EV_PROGRESS, NULL, pct);
        }
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(h)) err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "download: %s", esp_err_to_name(err));
        esp_https_ota_abort(h);
        emit(NANO_OTA_EV_ERROR, "Download interrupted", 0);
        return;
    }
    err = esp_https_ota_finish(h); /* verifies the image and makes it the boot partition */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "finish: %s", esp_err_to_name(err));
        emit(NANO_OTA_EV_ERROR, err == ESP_ERR_OTA_VALIDATE_FAILED ? "Downloaded image is damaged" : "Could not install", 0);
        return;
    }
    ESP_LOGI(TAG, "installed %s, restarting", desc.version);
    emit(NANO_OTA_EV_INSTALLED, desc.version, 100);
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
}

/* ---- task ------------------------------------------------------------------ */

static void ota_task(void *arg)
{
    (void)arg;
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s_cmds, &c, portMAX_DELAY) != pdTRUE) continue;
        switch (c.kind) {
        case CMD_SCAN:
            wifi_scan();
            break;
        case CMD_JOIN:
            if (wifi_connect(c.ssid, c.pass)) {
                creds_save(c.ssid, c.pass);
                emit(NANO_OTA_EV_WIFI_SAVED, c.ssid, 0);
                check();
            }
            break;
        case CMD_CHECK:
            check();
            break;
        case CMD_INSTALL:
            install();
            break;
        }
        ESP_LOGI(TAG, "free heap %u B (min %u B)", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    }
}

static void post(const cmd_t *c)
{
    if (!s_cmds || xQueueSend(s_cmds, c, 0) != pdTRUE) ESP_LOGW(TAG, "command %d dropped", (int)c->kind);
}

int nano_ota_init(void)
{
    if (!s_cmds) s_cmds = xQueueCreate(4, sizeof(cmd_t));
    return s_cmds ? 0 : -1;
}

int nano_ota_start(nano_ota_cb_t cb)
{
    if (s_started) return 0;
    s_cb = cb;
    ESP_LOGI(TAG, "update mode, free heap %u B", (unsigned)esp_get_free_heap_size());
    s_bits = xEventGroupCreate();
    if (nano_ota_init() != 0 || !s_bits) return -1;
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK; /* someone made it already */
    }
    if (err == ESP_OK) {
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&init);
    }
    if (err == ESP_OK) {
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_event, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);
        esp_wifi_set_storage(WIFI_STORAGE_RAM); /* the network lives in our own NVS keys */
        esp_wifi_set_mode(WIFI_MODE_STA);
        err = esp_wifi_start();
    }
    if (err == ESP_OK) esp_wifi_set_ps(WIFI_PS_NONE); /* no Bluetooth to share the radio with: full speed */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start: %s", esp_err_to_name(err));
        return -1;
    }
    if (xTaskCreatePinnedToCore(ota_task, "nano_ota", TASK_STACK, NULL, 4, NULL, 0) != pdPASS) return -1;
    s_started = true;
    return 0;
}

void nano_ota_scan(void)
{
    cmd_t c = { .kind = CMD_SCAN };
    post(&c);
}

void nano_ota_join(const char *ssid, const char *password)
{
    cmd_t c = { .kind = CMD_JOIN };
    strlcpy(c.ssid, ssid, sizeof(c.ssid));
    strlcpy(c.pass, password ? password : "", sizeof(c.pass));
    post(&c);
}

void nano_ota_check(void)
{
    cmd_t c = { .kind = CMD_CHECK };
    post(&c);
}

void nano_ota_install(void)
{
    cmd_t c = { .kind = CMD_INSTALL };
    post(&c);
}
