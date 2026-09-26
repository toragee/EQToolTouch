/*****************************************************************************
 *  Web_Server
 *  ESP32-C6 를 WiFi AP 로 띄우고, HTTP + WebSocket 으로 실시간 압력 데이터를
 *  브라우저(PC / 안드로이드)에 보낸다. 외부 라이브러리 없이 ESP-IDF 내장
 *  esp_http_server 만 사용한다.
 *
 *    접속: WiFi "EQTool" 에 연결 -> 브라우저에서 http://192.168.4.1
 *****************************************************************************/
#pragma once
#include <Arduino.h>

#define AP_SSID        "EQTool"
#define AP_PASS        "eqtool1234"   /* 8자 이상. "" 로 두면 개방형 AP */
#define AP_CHANNEL     6
#define AP_MAX_CONN    4

#define WS_FPS         20             /* 초당 프레임 수 */
#define WS_MAX_SAMPLES 96             /* 한 프레임에 담는 최대 샘플 수 */

/* EQTool.ino 에 정의. 보드에서 선택된 현재 Y축 범위(Pa)를 돌려준다. */
void     EQ_GetYRange(int16_t *lo, int16_t *hi);

bool     Web_Start(void);
uint32_t Web_GetClientCount(void);
