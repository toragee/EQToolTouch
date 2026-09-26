/*****************************************************************************
 *  LVGL_Driver.cpp  -  ESP32-C6-Touch-LCD-1.47
 *
 *  바뀐 점 (이전 ESP32-C6-LCD-1.47 대비)
 *   - flush 를 Arduino_GFX 의 draw16bitBeRGBBitmap 으로 (JD9853)
 *   - 드로우 버퍼를 static 배열 -> heap_caps_malloc (내부 RAM)
 *   - full_refresh 제거 (부분 버퍼라 사용 불가)
 *   - Lvgl_Touchpad_Read 를 AXS5106L 실제 구현으로 교체 (이전엔 더미)
 ****************************************************************************/
#include "LVGL_Driver.h"
#include "Touch_AXS5106.h"

static lv_disp_draw_buf_t  draw_buf;
static lv_color_t         *disp_draw_buf = NULL;

/* Serial debugging */
void Lvgl_print(const char *buf)
{
    Serial.printf("[LVGL] %s", buf);
    Serial.flush();
}

void Lvgl_Display_LCD(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    LCD_addWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t *)&color_p->full);
    lv_disp_flush_ready(disp_drv);
}

/* 터치패드 읽기 */
void Lvgl_Touchpad_Read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    LV_UNUSED(indev_drv);

    uint16_t x, y;
    if (Touch_GetPoint(&x, &y)) {
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

/* lv_conf.h 의 LV_TICK_CUSTOM 이 1 이면 LVGL 이 millis() 를 직접 읽으므로
   lv_tick_inc() 자체가 컴파일되지 않는다. 그래서 같이 조건부로 둔다. */
#if LV_TICK_CUSTOM == 0
void example_increase_lvgl_tick(void *arg)
{
    LV_UNUSED(arg);
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}
#endif

void Lvgl_Init(void)
{
    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb(Lvgl_print);
#endif

    disp_draw_buf = (lv_color_t *)heap_caps_malloc(LVGL_BUF_LEN * sizeof(lv_color_t),
                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_draw_buf) {
        Serial.println("[LVGL] draw buffer alloc failed - halted");
        while (1) delay(1000);
    }
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, LVGL_BUF_LEN);

    /* Display */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LVGL_WIDTH;
    disp_drv.ver_res  = LVGL_HEIGHT;
    disp_drv.flush_cb = Lvgl_Display_LCD;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Touch */
    Touch_Init();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = Lvgl_Touchpad_Read;
    lv_indev_drv_register(&indev_drv);

    /* lv_conf.h 가 LV_TICK_CUSTOM 1 이면 millis() 를 쓰므로 타이머가 필요 없다. */
#if LV_TICK_CUSTOM == 0
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);
#endif

    Serial.printf("[LVGL] init  %ux%u  buf=%u px (%u byte)  heap=%u\n",
                  (unsigned)LVGL_WIDTH, (unsigned)LVGL_HEIGHT,
                  (unsigned)LVGL_BUF_LEN, (unsigned)(LVGL_BUF_LEN * sizeof(lv_color_t)),
                  (unsigned)ESP.getFreeHeap());
}

void Timer_Loop(void)
{
    lv_timer_handler();
}
