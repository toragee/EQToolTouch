/*****************************************************************************
 *  Display_LCD.h  -  Waveshare ESP32-C6-Touch-LCD-1.47 (1.47" 172x320 IPS)
 *
 *  ※ 이전 보드(ESP32-C6-LCD-1.47)는 ST7789 를 직접 SPI 로 두드렸지만,
 *    터치 버전 보드는 컨트롤러가 JD9853 이라 전용 초기화 시퀀스가 필요하다.
 *    벤더 데모와 동일하게 Arduino_GFX(GFX_Library_for_Arduino) 를 쓴다.
 *
 *  핀맵 (ESP32-C6-Touch-LCD-1.47)
 *    LCD   : SCK=1  MOSI=2  CS=14  DC=15  RST=22  BL=23
 *    TF    : SCK=1  MOSI=2  MISO=3  CS=4          (SPI 공유)
 *    Touch : SDA=18 SCL=19 RST=20 INT=21          (QMI8658 IMU 와 공유)
 *    BAT   : GPIO0 (ADC)
 *    => ADC1(GPIO0~6) 중 남는 것은 GPIO5 / GPIO6 뿐. 압력센서는 GPIO5 사용.
 ****************************************************************************/
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

/* ===== 화면 방향 =====
 *   0 = 세로 172x320
 *   1 = 가로 320x172
 *   2 = 세로 뒤집힘
 *   3 = 가로 320x172 (1 을 180도 뒤집은 것)  <- 이전 보드와 같은 방향
 */
#define LCD_ROTATION   3

#if (LCD_ROTATION & 1)
  #define LCD_WIDTH    320
  #define LCD_HEIGHT   172
#else
  #define LCD_WIDTH    172
  #define LCD_HEIGHT   320
#endif

/* 패널 원본 해상도 / 오프셋 (JD9853 는 240 라인 중 172 만 쓴다) */
#define LCD_PANEL_W    172
#define LCD_PANEL_H    320
#define LCD_COL_OFFSET 34

/* ===== 핀 ===== */
#define LCD_PIN_SCK    1
#define LCD_PIN_MOSI   2
#define LCD_PIN_CS     14
#define LCD_PIN_DC     15
#define LCD_PIN_RST    22
#define LCD_PIN_BL     23

/* 패널 자가진단. 평소에는 0.
   화면이 안 나올 때 1 로 두면 부팅 시 회전 0/1/2/3 을 차례로 칠해서
   백라이트 / 패널 / 회전 중 어디가 문제인지 갈라준다. */
#define LCD_SELFTEST   0

/* 백라이트 PWM (ESP32 Arduino core 3.x LEDC API) */
#define LCD_BL_FREQ    5000
#define LCD_BL_BITS    10
#define LCD_BL_DEFAULT 100     /* 평상시 밝기 (%) */

extern Arduino_GFX *gfx;

void LCD_Init(void);

/* 런타임 회전 변경. 0/2 는 세로, 1/3 은 가로.
   해상도가 바뀌는 회전(가로<->세로)은 LVGL 쪽 설정도 같이 바꿔야 하므로
   여기서는 180도 뒤집기(1<->3, 0<->2)만 쓸 것.
   호출 후 화면 전체를 무효화해서 다시 그려야 한다. */
void LCD_SetRotation(uint8_t r);
uint8_t LCD_GetRotation(void);

/* LVGL flush 용. (Xend/Yend 는 포함 좌표) */
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart,
                   uint16_t Xend,   uint16_t Yend, uint16_t *color);

/* 백라이트 핀/PWM 만 준비하고 켜지는 않는다 (duty 0).
   부팅 시 검은 화면이 보이지 않도록, 첫 프레임을 그린 뒤에
   Set_Backlight() 으로 켜는 것이 호출자의 책임이다. */
/* 패널 자가진단 (회전 0/1/2/3 을 색으로 칠해본다).
   부팅 시 자동 실행은 LCD_SELFTEST 로 제어하고,
   런타임에는 시리얼 'l' 명령으로 언제든 부를 수 있다. */
void LCD_SelfTest(void);

void Backlight_Init(void);
void Set_Backlight(uint8_t percent);     /* 0 ~ 100 */

/* 슬립 직전에 호출. LEDC 를 떼고 핀을 순수 LOW 출력으로 고정한다.
   duty 0 에만 의존하면 슬립 중 LEDC 가 멈출 때 핀 상태가 애매해질 수 있다.
   ※ 보드의 전원 LED(LED2)는 VSYS 직결이라 펌웨어로는 끌 수 없다.
     대기전류를 줄이려면 R19(3K)를 제거해야 한다. */
void Backlight_Sleep(void);
