#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include "Display_LCD.h"

#define LVGL_WIDTH    LCD_WIDTH
#define LVGL_HEIGHT   LCD_HEIGHT

/* 부분 렌더링 버퍼: 가로 한 줄 x 40줄.
   320x40x2byte = 25.6KB. C6 는 PSRAM 이 없으므로 내부 RAM 에서 잡는다. */
#define LVGL_BUF_LINES  40
#define LVGL_BUF_LEN    (LVGL_WIDTH * LVGL_BUF_LINES)

#define EXAMPLE_LVGL_TICK_PERIOD_MS  5

void Lvgl_print(const char *buf);
void Lvgl_Display_LCD(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
void Lvgl_Touchpad_Read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
#if LV_TICK_CUSTOM == 0
void example_increase_lvgl_tick(void *arg);   /* LV_TICK_CUSTOM=1 이면 불필요 */
#endif

void Lvgl_Init(void);
void Timer_Loop(void);
