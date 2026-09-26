/*****************************************************************************
 *  Touch_AXS5106.h  -  ESP32-C6-Touch-LCD-1.47 정전식 터치 (AXS5106L, I2C 0x63)
 *
 *  실제 드라이버는 Waveshare 가 제공하는 라이브러리를 그대로 쓴다.
 *    libraries/esp_lcd_touch_axs5106l/
 *  여기서는 LVGL 이 쓰기 좋은 형태로 감싸기만 한다.
 ****************************************************************************/
#pragma once

#include <Arduino.h>
#include <Wire.h>

#define TOUCH_PIN_SDA  18
#define TOUCH_PIN_SCL  19
#define TOUCH_PIN_RST  20
#define TOUCH_PIN_INT  21
#define TOUCH_I2C_HZ   400000

/* LCD 초기화(=회전 결정) 이후에 호출할 것 */
void Touch_Init(void);

/* 화면 회전이 바뀌면 터치 좌표 변환도 같이 바꿔줘야 한다.
   벤더 드라이버는 bsp_touch_init() 에서 받은 회전값을 전역에 들고 있고
   세터가 없어서, 그 전역을 직접 갱신한다. */
void Touch_SetRotation(uint16_t r);

/* 눌려 있으면 true 와 좌표. 아니면 false. */
bool Touch_GetPoint(uint16_t *x, uint16_t *y);
