#include "Web_Server.h"
#include "Pressure_Sampler.h"
#include "web_page.h"

#include <WiFi.h>
#include <esp_http_server.h>

static httpd_handle_t   server     = NULL;
static volatile uint32_t client_cnt = 0;

/* ---------------- 프레임 포맷 (little endian) ------------------------------
 *   [0..3]   uint32  seq        프레임 번호
 *   [4..5]   uint16  n          샘플 개수
 *   [6..7]   uint16  rate_hz    실측 샘플링 주파수
 *   [8..9]   int16   y_min      보드에서 선택된 Y축 하한 (Pa)
 *   [10..11] int16   y_max      보드에서 선택된 Y축 상한 (Pa)
 *   [12.. ]  float32 pa[n]      압력 (Pa)
 * ------------------------------------------------------------------------ */
#define FRAME_HDR   12
static uint8_t frame[FRAME_HDR + WS_MAX_SAMPLES * 4];

/* ---------------- HTTP: 페이지 -------------------------------------------- */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, EQ_PAGE, HTTPD_RESP_USE_STRLEN);
}

/* 안드로이드/윈도우의 캡티브 포털 탐지 요청에 204 를 돌려줘
   "인터넷 없음" 팝업으로 연결이 끊기는 것을 막는다. */
static esp_err_t noop_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ---------------- WebSocket ----------------------------------------------- */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {          /* 핸드셰이크 */
        client_cnt++;
        Serial.printf("[WEB] client connected (%u)\n", (unsigned)client_cnt);
        return ESP_OK;
    }

    uint8_t buf[32] = {0};
    httpd_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    f.payload = buf;

    if (httpd_ws_recv_frame(req, &f, sizeof(buf) - 1) != ESP_OK) return ESP_FAIL;

    if (f.type == HTTPD_WS_TYPE_TEXT && f.len > 0) {
        if (buf[0] == 'z' || buf[0] == 'Z') {
            Serial.println("[WEB] re-zero requested");
            Pressure_Rezero();
        }
        else if (!strncmp((char *)buf, "lp:", 3)) {          /* 저역통과 Hz */
            Pressure_SetLowPass(atof((char *)buf + 3));
        }
        else if (!strncmp((char *)buf, "hp:", 3)) {          /* 고역통과 Hz */
            Pressure_SetHighPass(atof((char *)buf + 3));
        }
    }
    return ESP_OK;
}

static uint32_t ws_broadcast(const uint8_t *data, size_t len)
{
    int    fds[16];
    size_t n = sizeof(fds) / sizeof(fds[0]);

    esp_err_t r = httpd_get_client_list(server, &n, fds);
    if (r != ESP_OK) {
        static esp_err_t last_err = ESP_OK;
        if (r != last_err) {
            last_err = r;
            Serial.printf("[WEB] httpd_get_client_list failed: %s\n", esp_err_to_name(r));
        }
        return 0;
    }

    uint32_t sent = 0;
    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;

        httpd_ws_frame_t f;
        memset(&f, 0, sizeof(f));
        f.type    = HTTPD_WS_TYPE_BINARY;
        f.payload = (uint8_t *)data;
        f.len     = len;

        esp_err_t e = httpd_ws_send_frame_async(server, fds[i], &f);
        if (e == ESP_OK) {
            sent++;
        } else {
            static esp_err_t last_send_err = ESP_OK;
            if (e != last_send_err) {
                last_send_err = e;
                Serial.printf("[WEB] ws send failed: %s\n", esp_err_to_name(e));
            }
        }
    }
    client_cnt = sent;
    return sent;
}

/* ---------------- 송신 태스크 --------------------------------------------- */
static void web_task(void *arg)
{
    (void)arg;
    uint32_t seq = 0, t_log = 0;
    uint32_t st_frames = 0, st_samples = 0, st_sent = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000 / WS_FPS));

        /* 링버퍼는 항상 비운다. 보는 사람이 없어도 밀리지 않게. */
        uint16_t n  = 0;
        float   *pa = (float *)(frame + FRAME_HDR);
        float    v;
        while (n < WS_MAX_SAMPLES && Pressure_PopWeb(&v)) pa[n++] = v;

        if (n > 0) {
            uint16_t rate = (uint16_t)Pressure_GetRateHz();
            int16_t  ylo, yhi;
            EQ_GetYRange(&ylo, &yhi);

            memcpy(frame +  0, &seq,  4);
            memcpy(frame +  4, &n,    2);
            memcpy(frame +  6, &rate, 2);
            memcpy(frame +  8, &ylo,  2);
            memcpy(frame + 10, &yhi,  2);
            seq++;

            /* client_cnt 로 미리 거르지 않는다.
               한 번 전송에 실패해 0 이 되면 영영 안 보내지는 문제가 있었다. */
            st_sent += ws_broadcast(frame, FRAME_HDR + n * 4);
            st_frames++;
            st_samples += n;
        }

        uint32_t now = millis();
        if (now - t_log >= 1000) {
            t_log = now;
            Serial.printf("[WEB] ws=%u  frames=%u  samples=%u  sent=%u  rate=%u Hz\n",
                          (unsigned)client_cnt, (unsigned)st_frames,
                          (unsigned)st_samples, (unsigned)st_sent,
                          (unsigned)Pressure_GetRateHz());
            st_frames = st_samples = st_sent = 0;
        }
    }
}

/* ---------------- 시작 ----------------------------------------------------- */
bool Web_Start(void)
{
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(AP_SSID, (strlen(AP_PASS) >= 8) ? AP_PASS : NULL,
                     AP_CHANNEL, 0, AP_MAX_CONN)) {
        Serial.println("[WEB] softAP failed");
        return false;
    }

    httpd_config_t cfg      = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets    = AP_MAX_CONN + 2;
    cfg.lru_purge_enable    = true;
    cfg.stack_size          = 5120;
    cfg.uri_match_fn        = httpd_uri_match_wildcard;

    if (httpd_start(&server, &cfg) != ESP_OK) {
        Serial.println("[WEB] httpd_start failed");
        return false;
    }

    httpd_uri_t u_root = { "/",    HTTP_GET, root_handler, NULL };
    httpd_uri_t u_ws   = { "/ws",  HTTP_GET, ws_handler,   NULL, true, false, NULL };
    httpd_uri_t u_any  = { "/*",   HTTP_GET, noop_handler, NULL };
    httpd_register_uri_handler(server, &u_root);
    httpd_register_uri_handler(server, &u_ws);
    httpd_register_uri_handler(server, &u_any);

    xTaskCreate(web_task, "web", 4096, NULL, 1, NULL);

    Serial.printf("[WEB] AP \"%s\"  pass \"%s\"  ->  http://%s\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
    return true;
}

uint32_t Web_GetClientCount(void) { return client_cnt; }
