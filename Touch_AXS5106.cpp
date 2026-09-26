#include "Touch_AXS5106.h"
#include "Display_LCD.h"
#include "esp_lcd_touch_axs5106l.h"

static bool touch_ready = false;

void Touch_Init(void)
{
    Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL);
    Wire.setClock(TOUCH_I2C_HZ);

    /* 회전/해상도를 넘겨주면 드라이버가 좌표를 알아서 돌려준다 */
    bsp_touch_init(&Wire, TOUCH_PIN_RST, TOUCH_PIN_INT,
                   gfx->getRotation(), gfx->width(), gfx->height());
    touch_ready = true;

    Serial.printf("[Touch] AXS5106L init  SDA=%d SCL=%d RST=%d INT=%d  rot=%u\n",
                  TOUCH_PIN_SDA, TOUCH_PIN_SCL, TOUCH_PIN_RST, TOUCH_PIN_INT,
                  (unsigned)gfx->getRotation());
}

/* esp_lcd_touch_axs5106l.cpp 의 파일 스코프 전역 (static 이 아니라 접근 가능).
   bsp_touch_get_coordinates() 가 이 값으로 좌표를 회전시킨다. */
extern uint16_t g_rotation;

void Touch_SetRotation(uint16_t r)
{
    g_rotation = r;
}

bool Touch_GetPoint(uint16_t *x, uint16_t *y)
{
    if (!touch_ready) return false;

    touch_data_t td;
    bsp_touch_read();
    if (!bsp_touch_get_coordinates(&td)) return false;
    if (td.touch_num == 0)               return false;

    if (x) *x = td.coords[0].x;
    if (y) *y = td.coords[0].y;
    return true;
}
