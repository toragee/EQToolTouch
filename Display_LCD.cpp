/*****************************************************************************
 *  Display_LCD.cpp  -  JD9853 (Waveshare ESP32-C6-Touch-LCD-1.47)
 *
 *  lcd_reg_init() 의 레지스터 시퀀스는 Waveshare 데모
 *  (ESP32-C6-Touch-LCD-1.47-Demo/Arduino/examples/04_lvgl_arduino_v8) 에서
 *  그대로 가져온 것이다. 값의 의미는 JD9853 데이터시트 비공개라 수정 금지.
 ****************************************************************************/
#include <lvgl.h>
#include "Display_LCD.h"

static Arduino_DataBus *bus = new Arduino_HWSPI(LCD_PIN_DC  /* DC   */,
                                                LCD_PIN_CS  /* CS   */,
                                                LCD_PIN_SCK /* SCK  */,
                                                LCD_PIN_MOSI/* MOSI */);

/* Arduino_ST7789 클래스를 쓰지만 실제 패널은 JD9853 이다.
   주소창(0x2A/0x2B) 과 MADCTL(0x36) 문법이 같아서 그대로 동작한다.
   오프셋 (col1,row1,col2,row2) = (34,0,34,0)  */
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_PIN_RST, 0 /* rotation */,
                                      false /* IPS */,
                                      LCD_PANEL_W, LCD_PANEL_H,
                                      LCD_COL_OFFSET, 0,
                                      LCD_COL_OFFSET, 0);

static void lcd_reg_init(void)
{
    static const uint8_t init_operations[] = {
        BEGIN_WRITE,
        WRITE_COMMAND_8, 0x11,          /* sleep out */
        END_WRITE,
        DELAY, 120,

        BEGIN_WRITE,
        WRITE_C8_D16, 0xDF, 0x98, 0x53,
        WRITE_C8_D8,  0xB2, 0x23,

        WRITE_COMMAND_8, 0xB7,
        WRITE_BYTES, 4,
        0x00, 0x47, 0x00, 0x6F,

        WRITE_COMMAND_8, 0xBB,
        WRITE_BYTES, 6,
        0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

        WRITE_C8_D16, 0xC0, 0x44, 0xA4,
        WRITE_C8_D8,  0xC1, 0x16,

        WRITE_COMMAND_8, 0xC3,
        WRITE_BYTES, 8,
        0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

        WRITE_COMMAND_8, 0xC4,
        WRITE_BYTES, 12,
        0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

        WRITE_COMMAND_8, 0xC8,
        WRITE_BYTES, 32,
        0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
        0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

        WRITE_COMMAND_8, 0xD0,
        WRITE_BYTES, 5,
        0x04, 0x06, 0x6B, 0x0F, 0x00,

        WRITE_C8_D16, 0xD7, 0x00, 0x30,
        WRITE_C8_D8,  0xE6, 0x14,
        WRITE_C8_D8,  0xDE, 0x01,

        WRITE_COMMAND_8, 0xB7,
        WRITE_BYTES, 5,
        0x03, 0x13, 0xEF, 0x35, 0x35,

        WRITE_COMMAND_8, 0xC1,
        WRITE_BYTES, 3,
        0x14, 0x15, 0xC0,

        WRITE_C8_D16, 0xC2, 0x06, 0x3A,
        WRITE_C8_D16, 0xC4, 0x72, 0x12,
        WRITE_C8_D8,  0xBE, 0x00,
        WRITE_C8_D8,  0xDE, 0x02,

        WRITE_COMMAND_8, 0xE5,
        WRITE_BYTES, 3,
        0x00, 0x02, 0x00,

        WRITE_COMMAND_8, 0xE5,
        WRITE_BYTES, 3,
        0x01, 0x02, 0x00,

        WRITE_C8_D8, 0xDE, 0x00,
        WRITE_C8_D8, 0x35, 0x00,
        WRITE_C8_D8, 0x3A, 0x05,        /* 16bpp */

        WRITE_COMMAND_8, 0x2A,
        WRITE_BYTES, 4,
        0x00, 0x22, 0x00, 0xCD,         /* col 34 ~ 205 (172) */

        WRITE_COMMAND_8, 0x2B,
        WRITE_BYTES, 4,
        0x00, 0x00, 0x01, 0x3F,         /* row 0 ~ 319  (320) */

        WRITE_C8_D8, 0xDE, 0x02,

        WRITE_COMMAND_8, 0xE5,
        WRITE_BYTES, 3,
        0x00, 0x02, 0x00,

        WRITE_C8_D8, 0xDE, 0x00,
        WRITE_C8_D8, 0x36, 0x00,        /* MADCTL - setRotation 이 다시 씀 */
        WRITE_COMMAND_8, 0x21,          /* display inversion on */
        END_WRITE,

        DELAY, 10,

        BEGIN_WRITE,
        WRITE_COMMAND_8, 0x29,          /* display on */
        END_WRITE
    };
    bus->batchOperation(init_operations, sizeof(init_operations));
}

/* LEDC 채널을 못 잡으면 그냥 ON/OFF 로 떨어진다 (화면이 아예 안 켜지는 것보단 낫다) */
static bool bl_pwm = false;

void Backlight_Init(void)
{
    /* 꺼진 상태로 준비만 한다.
       부팅 직후 켜버리면 LVGL 이 첫 프레임을 그릴 때까지 1~2초 동안
       검은 화면이 그대로 보인다. 켜는 것은 호출자가 첫 프레임 뒤에 한다. */
    pinMode(LCD_PIN_BL, OUTPUT);
    digitalWrite(LCD_PIN_BL, LOW);

    bl_pwm = ledcAttach(LCD_PIN_BL, LCD_BL_FREQ, LCD_BL_BITS);
    Serial.printf("[LCD] backlight GPIO%d  pwm=%s  (off until first frame)\n",
                  LCD_PIN_BL, bl_pwm ? "on" : "FAILED (on/off only)");

    Set_Backlight(0);
}

void Set_Backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;

    if (bl_pwm) ledcWrite(LCD_PIN_BL, ((1u << LCD_BL_BITS) - 1) * percent / 100);
    else        digitalWrite(LCD_PIN_BL, percent ? HIGH : LOW);
}


/* 패널 자가진단.
   회전 0/1/2/3 을 차례로 고유한 색으로 칠하고 좌상단에 흰 사각형을 찍는다.
     - 색이 순서대로 바뀐다      : 백라이트/패널/회전 모두 정상
     - 아무것도 안 보인다        : 백라이트(LEDK/LEDA) 또는 패널/FPC 문제
     - 일부 회전만 보인다        : MADCTL MV(행/열 교환) 미지원
   LVGL 이 돌고 있는 중에 불러도 된다. 끝나고 화면을 다시 그리는 것은
   호출자의 몫이다 (lv_obj_invalidate + lv_refr_now). */
void Backlight_Sleep(void)
{
    if (bl_pwm) {
        ledcDetach(LCD_PIN_BL);     /* PWM 해제 */
        bl_pwm = false;
    }
    pinMode(LCD_PIN_BL, OUTPUT);
    digitalWrite(LCD_PIN_BL, LOW);  /* 트랜지스터 베이스 LOW -> 백라이트 off */
}

void LCD_SetRotation(uint8_t r)
{
    gfx->setRotation(r & 3);
}

uint8_t LCD_GetRotation(void)
{
    return gfx->getRotation();
}

void LCD_SelfTest(void)
{
    static const uint16_t    col[4]  = { RED, GREEN, BLUE, YELLOW };
    static const char *const name[4] = { "RED", "GREEN", "BLUE", "YELLOW" };

    Set_Backlight(LCD_BL_DEFAULT);          /* 보여야 하니 켠다 */

    for (uint8_t r = 0; r < 4; r++) {
        gfx->setRotation(r);
        gfx->fillScreen(col[r]);
        gfx->fillRect(0, 0, 40, 40, WHITE);
        Serial.printf("[LCD] selftest rot=%u -> %-6s  (%ux%u)  white square at top-left\n",
                      r, name[r], (unsigned)gfx->width(), (unsigned)gfx->height());
        Serial.flush();
        delay(1500);
    }

    gfx->setRotation(LCD_ROTATION);
    gfx->fillScreen(BLACK);
    Serial.printf("[LCD] selftest done -> rot=%d\n", LCD_ROTATION);
}

void LCD_Init(void)
{
    if (!gfx->begin()) {
        Serial.println("[LCD] gfx->begin() failed");
        return;
    }
    lcd_reg_init();                  /* JD9853 초기화 (begin 이후에 해야 한다) */
    gfx->setRotation(LCD_ROTATION);
    gfx->fillScreen(BLACK);

    Backlight_Init();

    Serial.printf("[LCD] JD9853 ready  %ux%u  rot=%d\n",
                  (unsigned)gfx->width(), (unsigned)gfx->height(), LCD_ROTATION);

#if LCD_SELFTEST
    LCD_SelfTest();
#endif
}

void LCD_addWindow(uint16_t Xstart, uint16_t Ystart,
                   uint16_t Xend,   uint16_t Yend, uint16_t *color)
{
    uint32_t w = (uint32_t)(Xend - Xstart) + 1;
    uint32_t h = (uint32_t)(Yend - Ystart) + 1;

#if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(Xstart, Ystart, color, w, h);
#else
    gfx->draw16bitRGBBitmap  (Xstart, Ystart, color, w, h);
#endif
}
