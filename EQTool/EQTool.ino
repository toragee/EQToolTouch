#include <math.h>

#include "Display_LCD.h"
#include "LVGL_Driver.h"
#include "XGZP6899A.h"
#include "Pressure_Sampler.h"
#include "Web_Server.h"
#include "Touch_AXS5106.h"
#include "Battery.h"
#include "IMU_Orient.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <soc/soc_caps.h>
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  #include <HWCDC.h>        /* HWCDC::isPlugged() - USB 케이블 연결 판정 */
#endif
#include "ui.h"

/* ===================== 차트 표시 설정 =========================================
 *  차트 내부 값은 항상 Pa 로 넣는다 (분해능 확보).
 *  Y축 라벨만 그리기 이벤트에서 kPa 로 바꿔 그린다.
 *
 *  LVGL 8 의 lv_coord_t 는 int16_t 라 Pa 단위로는 +-32767 Pa 가 한계다.
 *  (= +-32.7 kPa. 풀스케일 +-40 kPa 전체를 보려면 Pa 대신 hPa 로 넣어야 한다)
 *
 *  0 Pa 위치: 아래에서 20% 지점 -> 위쪽 80%, 아래쪽 20%
 *  major tick 6개라 눈금이 -5, 0, 5, 10, 15, 20 kPa 로 딱 떨어진다.
 * ========================================================================== */
/* Y축 프리셋. 보드의 BOOT 버튼을 누를 때마다 순환한다.
 *   ticks 는 major tick 수. 0 kPa 가 눈금선에 오려면
 *   (0 - min) 이 (max-min)/(ticks-1) 의 배수여야 한다. */
#define BOOT_BTN_PIN          9         /* ESP32-C6 온보드 BOOT 버튼 */

static const struct {
    int16_t     min, max;
    uint8_t     ticks;
    const char *name;
} Y_PRESETS[] = {
    { -5000, 20000, 6, "-5 ~ 20 kPa" },   /* 눈금 -5/0/5/10/15/20 */
    { -5000, 15000, 5, "-5 ~ 15 kPa" },   /* 눈금 -5/0/5/10/15    */
    { -2000,  8000, 6, "-2 ~ 8 kPa"  },   /* 눈금 -2/0/2/4/6/8 (간격 2 kPa)  */
};
#define Y_PRESET_CNT  (sizeof(Y_PRESETS) / sizeof(Y_PRESETS[0]))

static uint8_t y_preset = 0;
#define CHART_POINTS        100
#define CHART_WINDOW_MS    2000         /* 차트 가로축이 담는 시간 폭 */
#define DRAW_MS              40         /* 화면 갱신 25 fps */

/* ===================== 로그 설정 ==========================================
 *   LOG_PERIODIC : 1초마다 압력값을 계속 출력한다. 평소에는 0 을 권장.
 *                  실행 중 시리얼 모니터에서 'p' 를 보내면 토글된다.
 *   그 외(부팅 정보, 센서 초기화 결과, 경고)는 항상 출력된다.
 *
 *   시리얼 명령:  p = 주기 로그 토글 / z = 영점 재설정 / i = 현재 상태 1회
 * ========================================================================= */
#define LOG_PERIODIC          0

/* ===== 전원 끄기 =========================================================
 *  진입은 30초 무변화 자동 슬립(또는 시리얼 'o')뿐이다. 화면 롱프레스는 쓰지 않는다.
 *
 *  이 보드에는 전원을 물리적으로 끊는 회로가 없다. 그래서 "전원 끄기" 는
 *  라이트 슬립이고, 깨우기는 터치 컨트롤러의 INT(GPIO21) 레벨 웨이크업이다.
 *
 *  왜 딥 슬립이 아닌가: ESP32-C6 의 딥 슬립 GPIO 웨이크업은 GPIO0~7 만
 *  지원한다. 터치 INT 는 GPIO21 이라 딥 슬립에서는 못 깨운다.
 *
 *  AXS5106L 은 INT 가 평소 HIGH 이고 터치 시 LOW 로 떨어진다
 *  (벤더 드라이버가 FALLING 엣지로 attachInterrupt 한다).
 *  따라서 GPIO_INTR_LOW_LEVEL 로 깨운다.
 *
 *  깨어난 뒤에는 복구하지 않고 esp_restart() 한다. WiFi AP / 샘플러 /
 *  차트를 부분 복구하는 것보다 재부팅이 훨씬 안전하고, 1초면 올라온다.
 * ====================================================================== */
/* --- 자동 슬립 -----------------------------------------------------------
 *  압력이 AUTO_SLEEP_PA 이상 움직이지 않은 채 AUTO_SLEEP_MS 가 지나면 잔다.
 *  터치, 시리얼 명령, 웹 클라이언트 접속 중에는 타이머가 계속 리셋된다.
 *  AUTO_SLEEP_MS 를 0 으로 두면 기능이 꺼진다.
 *  AUTO_SLEEP_PA 는 센서 노이즈보다 넉넉히 크게 잡아야 한다. 저역통과를
 *  거친 값을 쓰므로 200 Pa(0.2 kPa)면 충분하다. 너무 작으면 영영 안 잔다.
 * ---------------------------------------------------------------------- */
#define AUTO_SLEEP_MS       30000   /* 무변화 지속 시간. 0 = 기능 끔 */
#define AUTO_SLEEP_WARN_MS  10000   /* 남은 시간이 이보다 적으면 경고 표시 */
#define AUTO_SLEEP_PA        200.0f /* 이보다 작은 변화는 "변화 없음" 으로 본다 */

/*  USB 가 꽂혀 있으면 슬립하지 않는다.
 *  라이트 슬립에 들어가면 USB CDC 가 끊어져 COM 포트가 사라진다.
 *  개발 중에는 이게 계속 방해가 되고, USB 로 전원을 받는 중이라면
 *  배터리를 아낄 이유도 없다.
 *
 *  판정은 Serial.isPlugged() 로 한다. 내부적으로 usb_serial_jtag_is_connected()
 *  이고 호스트의 SOF 프레임을 보기 때문에, 시리얼 모니터를 열지 않아도
 *  케이블이 꽂혀 있으면 true 다. (if (Serial) 은 모니터를 닫으면 false 가 된다.)
 *
 *  적용 범위: 자동 슬립.
 *  시리얼 'o' 명령만은 강제로 재운다 (개발 중 슬립 동작 확인용).
 *  0 으로 두면 USB 와 무관하게 전부 동작한다. */
#define SLEEP_SKIP_ON_USB   1

/* --- 화면 자동 회전 (180도) ---------------------------------------------
 *  가속도 센서로 기기가 뒤집혔는지 보고 화면을 180도 돌린다.
 *  해상도가 바뀌지 않는 1 <-> 3 회전만 쓰므로 LVGL 설정은 건드릴 필요가 없다.
 *  (0 <-> 2 는 세로용. 가로 화면인 우리는 1/3 만 쓴다.)
 *
 *  ORIENT_AXIS / ORIENT_SIGN 은 보드에 IMU 가 어떤 방향으로 붙어 있느냐에
 *  달렸다. 시리얼 'a' 로 현재 가속도를 찍어보고 맞추면 된다.
 *  기기를 평소 방향으로 세웠을 때 해당 축이 +, 뒤집으면 - 가 되어야 한다.
 * --------------------------------------------------------------------- */
#define ORIENT_ENABLE       1
#define ORIENT_AXIS         'x'     /* 'x' / 'y' / 'z' */
#define ORIENT_SIGN         (-1)    /* 축은 맞고 방향만 반대여서 뒤집었다 (+1 <-> -1) */
#define ORIENT_THRESHOLD    0.35f   /* g. 이보다 기울어야 전환을 고려 */
#define ORIENT_HOLD_MS       700    /* 이만큼 유지돼야 실제로 전환 (떨림 방지) */
#define ORIENT_POLL_MS       200    /* IMU 읽는 주기 */
#define ORIENT_ROT_NORMAL      3    /* 평소 방향 */
#define ORIENT_ROT_FLIPPED     1    /* 180도 뒤집힘 */

static lv_obj_t     *po_label     = NULL;   /* 자동 슬립 카운트다운 표시 */
static volatile bool power_off_req = false; /* 실제 종료는 loop() 에서 */

static uint32_t idle_t0  = 0;       /* 마지막 "활동" 시각 */
static float    idle_ref = 0.0f;    /* 그때의 압력 (Pa) */

static lv_chart_series_t *press_ser   = NULL;
static lv_chart_series_t *zero_ser    = NULL;   /* 0 기준선 */
static bool               log_periodic = (LOG_PERIODIC != 0);

/* USB 케이블이 꽂혀 있는가 (호스트 SOF 기준. 모니터 개폐와 무관) */
static inline bool usb_plugged(void)
{
#if SLEEP_SKIP_ON_USB && SOC_USB_SERIAL_JTAG_SUPPORTED
    /* 클래스의 static 멤버를 직접 부른다.
       Serial.isPlugged() 로 쓰면 Tools -> USB CDC On Boot 가 Disabled 일 때
       Serial 이 HardwareSerial(UART0) 이라 컴파일이 깨진다.
       HWCDC::isPlugged() 는 usb_serial_jtag_is_connected() 래퍼라
       CDC 설정과 무관하게 USB 호스트 연결 여부를 알려준다. */
    return HWCDC::isPlugged();
#else
    return false;
#endif
}

/* 뭔가 "활동" 이 있었다. 자동 슬립 타이머를 처음부터 다시 센다. */
static void activity_reset(void)
{
    idle_t0  = millis();
    idle_ref = Pressure_GetFilteredPa();
}

/* 길게 누르는 동안 남은 시간을 표시한다. 손을 떼면 취소. */
static void po_label_show(const char *what, int sec_left)
{
    if (!po_label) return;
    lv_label_set_text_fmt(po_label, "%s %d", what, sec_left);
    lv_obj_clear_flag(po_label, LV_OBJ_FLAG_HIDDEN);
}

static void po_label_hide(void)
{
    if (po_label) lv_obj_add_flag(po_label, LV_OBJ_FLAG_HIDDEN);
}

#if ORIENT_ENABLE
static uint8_t       cur_rot = ORIENT_ROT_NORMAL;
static volatile int8_t rot_req = -1;     /* 실제 적용은 loop() 에서 */

/* 선택한 축의 가속도(g)를 돌려준다 */
static float orient_axis_g(float ax, float ay, float az)
{
    float g;
    switch (ORIENT_AXIS) {
        case 'y': g = ay; break;
        case 'z': g = az; break;
        default:  g = ax; break;
    }
    return g * ORIENT_SIGN;
}

/* ORIENT_POLL_MS 마다 IMU 를 보고, 임계값을 ORIENT_HOLD_MS 동안 계속
   넘으면 회전 요청을 건다. 손에서 잠깐 흔들리는 정도로는 안 바뀐다. */
static void orient_poll(void)
{
    static uint32_t t_poll = 0, t_cand = 0;
    static uint8_t  cand   = 0xFF;

    uint32_t now = millis();
    if (now - t_poll < ORIENT_POLL_MS) return;
    t_poll = now;

    float ax, ay, az;
    if (!IMU_ReadAccel(&ax, &ay, &az)) return;

    float   g    = orient_axis_g(ax, ay, az);
    uint8_t want = cur_rot;

    if      (g >  ORIENT_THRESHOLD) want = ORIENT_ROT_NORMAL;
    else if (g < -ORIENT_THRESHOLD) want = ORIENT_ROT_FLIPPED;
    /* 임계값 사이(눕혀놓은 상태 등)에서는 현재 방향을 유지한다 */

    if (want == cur_rot) { cand = 0xFF; return; }

    if (cand != want) { cand = want; t_cand = now; return; }

    if (now - t_cand >= ORIENT_HOLD_MS) {
        cand    = 0xFF;
        rot_req = (int8_t)want;
    }
}

/* 패널 회전 + 터치 좌표 변환을 같이 바꾸고 화면을 통째로 다시 그린다.
   LVGL 이벤트/타이머 안에서 부르면 안 된다. loop() 에서만 호출할 것. */
static void Screen_SetRotation(uint8_t r)
{
    cur_rot = r;

    LCD_SetRotation(r);
    Touch_SetRotation(r);

    /* 회전이 바뀌면 화면 좌표 매핑이 통째로 달라진다.
       부분 갱신으로는 이전 내용이 남으므로 전체를 무효화한다. */
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);

    Serial.printf("[EQTool] screen rotation -> %u (%s)\n",
                  r, (r == ORIENT_ROT_NORMAL) ? "normal" : "flipped");
}
#endif /* ORIENT_ENABLE */

static void draw_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    poll_boot_button();

#if ORIENT_ENABLE
    orient_poll();
#endif

    static double   acc   = 0.0;
    static uint32_t n     = 0;
    static uint32_t decim = 1;
    static uint32_t t_log = 0;

    uint32_t rate = Pressure_GetRateHz();
    if (rate > 0) {
        uint32_t d = (rate * CHART_WINDOW_MS / 1000) / CHART_POINTS;
        decim = (d < 1) ? 1 : d;
    }

    float pa;
    while (Pressure_Pop(&pa)) {
        acc += pa;
        if (++n >= decim) {
            long v = lroundf((float)(acc / n));          /* Pa 그대로 */
            if (v < Y_PRESETS[y_preset].min) v = Y_PRESETS[y_preset].min;
            if (v > Y_PRESETS[y_preset].max) v = Y_PRESETS[y_preset].max;
            if (press_ser) lv_chart_set_next_value(ui_Chart1, press_ser, (lv_coord_t)v);
            acc = 0.0;
            n   = 0;
        }
    }

    /* ===== 자동 슬립 ====================================================
       압력이 오래 안 움직이면 잔다.

       ※ 센서가 없거나 고장이어도 잔다. 예전에는 이 블록 전체를
         Pressure_IsOk() 로 감싸는 바람에, 센서를 안 꽂은 보드가 영영
         깨어 있으면서 배터리만 먹었다. 센서가 없으면 압력 변화가 있을
         리 없으니, 오히려 자야 하는 상황이다. */
#if AUTO_SLEEP_MS > 0
    if (!power_off_req) {
        /* 웹으로 누가 보고 있으면 자지 않는다 */
        if (Web_GetClientCount() > 0) activity_reset();

        /* USB 가 꽂혀 있으면 자지 않는다 */
        if (usb_plugged()) activity_reset();

        /* 압력 변화는 센서가 정상일 때만 "활동" 으로 친다 */
        static bool was_ok = false;
        bool        now_ok = Pressure_IsOk();

        if (now_ok) {
            if (!was_ok) {
                /* 막 정상이 됐다. 기준값이 낡았으므로 다시 잡는다
                   (부팅 직후 오토제로가 끝나는 시점이 여기다) */
                activity_reset();
            } else {
                float cur = Pressure_GetFilteredPa();
                if (fabsf(cur - idle_ref) > AUTO_SLEEP_PA) activity_reset();
            }
        }
        was_ok = now_ok;

        uint32_t idle = millis() - idle_t0;

        if (idle >= AUTO_SLEEP_MS) {
            Serial.printf("[EQTool] idle %us (%s) -> auto sleep\n",
                          (unsigned)(AUTO_SLEEP_MS / 1000),
                          Pressure_IsOk() ? "no pressure change" : "sensor not connected");
            power_off_req = true;
        } else if (idle >= AUTO_SLEEP_MS - AUTO_SLEEP_WARN_MS) {
            po_label_show("AUTO OFF IN",
                          (int)((AUTO_SLEEP_MS - idle + 999) / 1000));
        } else {
            po_label_hide();
        }
    }
#endif

    uint32_t now = millis();
    if (now - t_log >= 1000) {
        t_log = now;

        /* --- 경고: 센서 미준비 (5초에 한 번만) --- */
        if (!Pressure_IsOk()) {
            static uint32_t t_warn = 0;
            if (now - t_warn >= 5000) {
                t_warn = now;
                Serial.printf("[EQTool] waiting for sensor - OUT=GPIO%d reads %.1f mV\n",
                              XGZP_ADC_PIN, XGZPA_ReadMv());
            }
            return;
        }

        /* --- 경고: 링버퍼 오버런 (발생했을 때만) --- */
        static uint32_t last_drop = 0;
        uint32_t drop = Pressure_GetDropped();
        if (drop != last_drop) {
            Serial.printf("[EQTool] WARNING: %u samples dropped (UI too slow)\n",
                          (unsigned)(drop - last_drop));
            last_drop = drop;
        }

        /* --- 주기 로그 (기본 off) --- */
        if (log_periodic)
            Serial.printf("P=%8.1f Pa   out=%7.1f mV   rate=%u Hz\n",
                          Pressure_GetFilteredPa(), Pressure_GetLastMv(),
                          (unsigned)Pressure_GetRateHz());
    }
}

/* 시리얼 한 글자 명령 */
static void Serial_Command(void)
{
    while (Serial.available()) {
        activity_reset();
        switch (Serial.read()) {
        case 'p':
        case 'P':
            log_periodic = !log_periodic;
            Serial.printf("[EQTool] periodic log %s\n", log_periodic ? "ON" : "OFF");
            break;
        case 'z':
        case 'Z':
            Serial.println("[EQTool] re-zeroing - keep both ports open");
            Pressure_Rezero();
            break;
        case 'a':
        case 'A': {
            float ax = 0, ay = 0, az = 0;
            if (!IMU_ReadAccel(&ax, &ay, &az)) {
                Serial.println("[IMU] not available");
                break;
            }
#if ORIENT_ENABLE
            Serial.printf("[IMU] ax=%+.2f  ay=%+.2f  az=%+.2f g   "
                          "axis '%c' x %d = %+.2f   (threshold %.2f)\n",
                          ax, ay, az, ORIENT_AXIS, ORIENT_SIGN,
                          orient_axis_g(ax, ay, az), ORIENT_THRESHOLD);
#else
            Serial.printf("[IMU] ax=%+.2f  ay=%+.2f  az=%+.2f g\n", ax, ay, az);
#endif
            break;
        }
        case 'l':
        case 'L':
            Serial.println("[EQTool] LCD selftest (6s) - watch the panel");
            LCD_SelfTest();
            lv_obj_invalidate(lv_scr_act());   /* 자가진단이 덮어쓴 화면 복구 */
            lv_refr_now(NULL);
            Set_Backlight(LCD_BL_DEFAULT);
            break;
        case 'o':
        case 'O':
            Serial.println("[EQTool] power off requested (serial, forced)");
            power_off_req = true;      /* USB 가 꽂혀 있어도 강제로 잔다 */
            break;
        case 'i':
        case 'I':
            Serial.printf("[EQTool] P=%.1f Pa  out=%.1f mV  zero=%.1f mV  rate=%u Hz  drop=%u  heap=%u\n",
                          Pressure_GetFilteredPa(), Pressure_GetLastMv(),
                          XGZPA_GetZeroMv(), (unsigned)Pressure_GetRateHz(),
                          (unsigned)Pressure_GetDropped(), (unsigned)ESP.getFreeHeap());
            break;
        default:
            break;
        }
    }
}

/* Y축 눈금 라벨을 kPa 로 다시 쓴다.
   차트 값은 Pa 이므로 1000 으로 나눠 소수 첫째 자리까지 표기한다. */
static void chart_draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);

    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL))
        return;
    if (dsc->id != LV_CHART_AXIS_PRIMARY_Y || dsc->text == NULL)
        return;

    int32_t pa    = dsc->value;
    bool    minus = (pa < 0);
    int32_t a     = minus ? -pa : pa;

    int whole = (int)(a / 1000);
    int frac  = (int)((a % 1000) / 100);

    /* 소수 자리가 0 이면 아예 빼서 "0.0" 대신 "0" 으로 보이게 한다.
       프리셋 눈금이 전부 정수 kPa 라 평소에는 소수점이 나오지 않는다. */
    if (frac == 0)
        lv_snprintf(dsc->text, dsc->text_length, "%s%d", minus ? "-" : "", whole);
    else
        lv_snprintf(dsc->text, dsc->text_length, "%s%d.%d", minus ? "-" : "", whole, frac);
}

/* 웹(Web_Server.cpp)이 현재 Y축 범위를 프레임에 실어 보내기 위해 호출한다 */
void EQ_GetYRange(int16_t *lo, int16_t *hi)
{
    if (lo) *lo = Y_PRESETS[y_preset].min;
    if (hi) *hi = Y_PRESETS[y_preset].max;
}

/* Y축 범위/눈금을 프리셋에 맞춰 갱신한다 */
static void apply_y_preset(uint8_t i)
{
    y_preset = i % Y_PRESET_CNT;
    const uint8_t t = Y_PRESETS[y_preset].ticks;

    lv_chart_set_range(ui_Chart1, LV_CHART_AXIS_PRIMARY_Y,
                       Y_PRESETS[y_preset].min, Y_PRESETS[y_preset].max);
    lv_chart_set_axis_tick(ui_Chart1, LV_CHART_AXIS_PRIMARY_Y, 10, 5, t, 2, true, 40);
    lv_chart_set_div_line_count(ui_Chart1, t, 5);
    lv_chart_refresh(ui_Chart1);

    Serial.printf("[EQTool] Y range -> %s\n", Y_PRESETS[y_preset].name);
}

/* BOOT 버튼(GPIO9) 폴링. 40ms 안정화 후 눌린 순간에만 반응한다.
   GPIO9 는 스트래핑 핀이라 리셋 시점에만 부팅 모드에 영향을 준다.
   실행 중에 누르는 것은 무해하다.
   터치와 같은 동작을 하며, 둘 다 쓸 수 있다. */
static void poll_boot_button(void)
{
    static bool     stable   = true;    /* 풀업이라 안 눌린 상태가 HIGH */
    static bool     raw_prev = true;
    static uint32_t t_last   = 0;

    bool     raw = digitalRead(BOOT_BTN_PIN);
    uint32_t ms  = millis();

    if (raw != raw_prev) { raw_prev = raw; t_last = ms; return; }
    if (ms - t_last < 40) return;                  /* 디바운스 */

    if (raw != stable) {
        stable = raw;
        if (!stable) apply_y_preset(y_preset + 1); /* HIGH -> LOW = 눌림 */
    }
}

/* 화면을 짧게 터치하면 Y축 프리셋이 순환한다.
   (이전에는 BOOT 버튼(GPIO9)으로 했다. 터치 패널이 생겨서 그쪽으로 옮겼다.)

   LV_EVENT_SHORT_CLICKED 는 누른 시간이 LVGL 의 long-press 기준(기본 400ms)보다
   짧고, 손가락을 뗀 위치가 누른 위치와 같을 때만 발생한다. 즉 "짧은 탭" 이다.
   길게 누르기나 드래그(차트 스크롤)에는 반응하지 않는다.

   차트가 화면 대부분을 덮고 있고 LVGL 은 기본적으로 이벤트를 부모로 올리지
   않으므로, 화면과 차트 양쪽에 같은 콜백을 건다. */
#define TOUCH_MIN_INTERVAL_MS  300      /* 이보다 빠른 연속 탭은 무시 (떨림 방지) */

/* 화면을 건드리면 자동 슬립 타이머를 다시 센다.
   (짧은 탭은 touch_tap_cb 가 따로 받는다. 여기서는 누르는 순간만 본다.) */
static void touch_press_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) activity_reset();
}

/* 라이트 슬립으로 들어간다. 터치하면 깨어나고, 깨면 그냥 재부팅한다. */
static void Power_Off(void)
{
    Serial.println("[EQTool] power off -> light sleep  (touch screen to wake)");
    Serial.flush();

    /* 1. 화면에 마지막 상태를 한 번 그려준 뒤 끈다 */
    lv_timer_handler();
    delay(150);
    Backlight_Sleep();          /* LEDC 떼고 핀을 LOW 로 고정 */
    gfx->displayOff();

    /* 2. WiFi 를 내린다. AP 가 살아 있으면 라이트 슬립이 유지되지 않는다. */
    WiFi.mode(WIFI_OFF);
    delay(50);

    /* 3. 손을 뗄 때까지 기다린다.
          INT 가 LOW 인 채로 슬립에 들어가면 즉시 깨버린다. */
    pinMode(TOUCH_PIN_INT, INPUT_PULLUP);
    uint32_t t0 = millis();
    while (digitalRead(TOUCH_PIN_INT) == LOW && (millis() - t0 < 5000)) {
        Touch_GetPoint(NULL, NULL);        /* 읽어서 INT 를 해제시킨다 */
        delay(20);
    }
    delay(100);

    /* 4. 터치 INT LOW 레벨로 깨우도록 등록하고 잔다 */
    gpio_wakeup_enable((gpio_num_t)TOUCH_PIN_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    Serial.flush();
    esp_light_sleep_start();               /* ← 여기서 멈춘다 */

    /* 5. 깨어남. 부분 복구 대신 깔끔하게 재부팅한다. */
    gpio_wakeup_disable((gpio_num_t)TOUCH_PIN_INT);
    Serial.println("[EQTool] wake by touch -> restart");
    Serial.flush();
    delay(50);
    esp_restart();
}

static void touch_tap_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    static uint32_t t_last = 0;
    uint32_t        now    = millis();

    if (now - t_last < TOUCH_MIN_INTERVAL_MS) return;
    t_last = now;

    activity_reset();
    apply_y_preset(y_preset + 1);
}

/* ===== LVGL 8.4.0 버그 우회 =========================================
 *  lv_chart_add_series() 는 LINE/BAR 차트에서 ser->x_points 와
 *  ser->x_ext_buf_assigned 를 초기화하지 않는다 (SCATTER 일 때만 채운다).
 *  시리즈 노드는 lv_mem_alloc 으로 잡히므로 두 필드에는 쓰레기가 들어간다.
 *
 *  그런데 8.4.0 의 lv_chart_remove_series() 는
 *      if (!ser->x_ext_buf_assigned && ser->x_points) lv_mem_free(ser->x_points);
 *  를 실행한다. → 쓰레기 포인터를 free 하다가 Store access fault 로 죽는다.
 *
 *  LVGL 8.3.10 의 remove_series 에는 이 줄이 없어서 이전 보드
 *  (ESP32-C6-LCD-1.47) 에서는 드러나지 않았다.
 *
 *  시리즈를 만들거나 넘겨받은 직후에 x 쪽을 직접 비워 두면 안전하다.
 * ================================================================== */
static void chart_fix_series_x(lv_chart_series_t *ser)
{
    if (!ser) return;
    ser->x_points           = NULL;
    ser->x_ext_buf_assigned = 0;
}

/* ===== 부팅 시 배터리 표시 =============================================
 *  최상위 레이어에 전체 화면 패널을 띄우고 BAT_SPLASH_MS 뒤에 지운다.
 *  화면을 터치하면 즉시 건너뛴다.
 *  블로킹하지 않으므로 그 사이에도 샘플링과 차트는 정상 동작한다.
 * ===================================================================== */
#define BAT_SPLASH_MS   5000

static lv_obj_t *bat_splash = NULL;

static void bat_splash_close(bool async)
{
    if (!bat_splash) return;
    lv_obj_t *o = bat_splash;
    bat_splash = NULL;                 /* 재진입 방지 */
    if (async) lv_obj_del_async(o);    /* 이벤트 콜백 안에서는 async 로 */
    else       lv_obj_del(o);
}

static void bat_splash_timer_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    bat_splash_close(false);
}

static void bat_splash_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    activity_reset();                  /* 자동 슬립 타이머도 리셋 */
    bat_splash_close(true);            /* 콜백 안 -> async 삭제 */
}

static void Battery_Splash(void)
{
    float   mv  = Battery_ReadMv();
    uint8_t pct = Battery_Percent(mv);
    bool    usb = usb_plugged();

    Serial.printf("[BAT] %.0f mV  %u%%%s\n", mv, (unsigned)pct, usb ? "  (USB - charging)" : "");

    /* 전체 화면 패널 */
    bat_splash = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bat_splash);
    lv_obj_set_size(bat_splash, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bat_splash, lv_color_hex(0x0C1F40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (bat_splash, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_clear_flag(bat_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag  (bat_splash, LV_OBJ_FLAG_CLICKABLE);   /* 터치로 건너뛰기 */
    lv_obj_add_event_cb(bat_splash, bat_splash_click_cb, LV_EVENT_CLICKED, NULL);

    /* 제목 */
    lv_obj_t *title = lv_label_create(bat_splash);
    lv_label_set_text(title, "EQTool");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8FB6FF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    /* 배터리 막대 */
    lv_obj_t *bar = lv_bar_create(bat_splash);
    lv_obj_set_size(bar, 200, 34);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, -6);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16305C), LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x8FB6FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    /* 잔량에 따라 색: 20% 이하 빨강, 50% 이하 노랑, 그 위 초록 */
    lv_obj_set_style_bg_color(bar,
        lv_color_hex(pct <= 20 ? 0xFF5A5A : (pct <= 50 ? 0xFFD05A : 0x00E05A)),
        LV_PART_INDICATOR);

    /* 막대 오른쪽 끝의 단자 모양 */
    lv_obj_t *nub = lv_obj_create(bat_splash);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 6, 14);
    lv_obj_align_to(nub, bar, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_set_style_bg_color(nub, lv_color_hex(0x8FB6FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (nub, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_radius  (nub, 2,                      LV_PART_MAIN);

    /* 수치.
       주의: LVGL 의 lv_snprintf 는 %f 를 지원하지 않는다
       (lv_conf.h 의 LV_SPRINTF_USE_FLOAT 가 0). %.2f 를 쓰면 화면에 "f" 가
       그대로 찍힌다. 정수 두 개로 쪼개서 소수점을 직접 만든다. */
    uint32_t mvi = (uint32_t)(mv + 0.5f);
    lv_obj_t *txt = lv_label_create(bat_splash);
    lv_label_set_text_fmt(txt, "%u %%   %u.%02u V%s",
                          (unsigned)pct,
                          (unsigned)(mvi / 1000),
                          (unsigned)((mvi % 1000) / 10),
                          usb ? "   CHARGING" : "");
    lv_obj_set_style_text_color(txt, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(txt, LV_ALIGN_CENTER, 0, 34);

    lv_timer_create(bat_splash_timer_cb, BAT_SPLASH_MS, NULL);
}

static void Chart_Init(void)
{
    /* SquareLine 이 붙여둔 고정 배열(ext array) 시리즈를 제거하고 새로 만든다.
       그 배열은 값이 80개뿐인데 point_count 는 100 이라 그대로 두면 버퍼 오버런. */
    lv_chart_series_t *old = lv_chart_get_series_next(ui_Chart1, NULL);
    if (old) {
        chart_fix_series_x(old);                  /* ← 위 버그 우회. 빼면 죽는다 */
        lv_chart_remove_series(ui_Chart1, old);
    }

    lv_chart_set_type(ui_Chart1, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ui_Chart1, CHART_POINTS);
    /* 범위/눈금은 프리셋이 정한다 (아래 apply_y_preset) */

    /* 라벨을 kPa 로 바꿔 그린다 */
    lv_obj_add_event_cb(ui_Chart1, chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    /* 내부 눈금선.
       가로줄은 위/아래 끝을 포함해 균등 분할되므로, 축 tick 과 같은 6줄을 주면
       -5 / 0 / 5 / 10 / 15 / 20 kPa 즉 5 kPa 간격으로 정확히 떨어진다.
       세로줄 5개 = 2초 창을 0.5초 간격으로 나눈다. */
    lv_chart_set_div_line_count(ui_Chart1, Y_PRESETS[0].ticks, 5);

    /* 눈금선 스타일 (LV_PART_MAIN. 데이터 선은 LV_PART_ITEMS 라 영향 없음) */
    lv_obj_set_style_line_color(ui_Chart1, lv_color_hex(0x2A4A7A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(ui_Chart1, 1,                      LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa  (ui_Chart1, LV_OPA_60,              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_update_mode(ui_Chart1, LV_CHART_UPDATE_MODE_SHIFT);

    /* 오른쪽 보조 Y축 제거.
       인자: (obj, axis, major_len, minor_len, major_cnt, minor_cnt, label_en, draw_size) */
    lv_chart_set_axis_tick(ui_Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 0, 0, false, 0);

    /* 0 기준선(노랑)을 먼저 추가한다.
       LVGL 은 먼저 추가된 시리즈를 아래에 그리므로, 데이터 선이 위에 온다. */
    zero_ser = lv_chart_add_series(ui_Chart1, lv_color_hex(0xFFE98A),
                                   LV_CHART_AXIS_PRIMARY_Y);
    chart_fix_series_x(zero_ser);
    if (zero_ser) lv_chart_set_all_value(ui_Chart1, zero_ser, 0);
    else          Serial.println("[EQTool] ERROR: zero series alloc failed (LV_MEM_SIZE?)");

    press_ser = lv_chart_add_series(ui_Chart1, lv_color_hex(0x00E05A),
                                    LV_CHART_AXIS_PRIMARY_Y);
    chart_fix_series_x(press_ser);
    if (press_ser) lv_chart_set_all_value(ui_Chart1, press_ser, 0);
    else           Serial.println("[EQTool] ERROR: data series alloc failed (LV_MEM_SIZE?)");

    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);        /* BOOT 버튼도 그대로 쓴다 */

    /* 짧은 화면 터치 -> Y축 프리셋 순환 (BOOT 버튼과 동일 동작) */
    lv_obj_add_flag(ui_Chart1,  LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui_Screen1, LV_OBJ_FLAG_CLICKABLE);

    /* 차트 스크롤을 끈다. 켜져 있으면 손가락이 조금만 흔들려도 스크롤로
       인식되어 탭(SHORT_CLICKED)이 발생하지 않는다. 차트를 스크롤할 일도 없다. */
    lv_obj_clear_flag(ui_Chart1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ui_Chart1,  touch_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Screen1, touch_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* 화면을 건드리면 자동 슬립 타이머 리셋 */
    lv_obj_add_event_cb(ui_Chart1,  touch_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_Screen1, touch_press_cb, LV_EVENT_PRESSED, NULL);

    /* 자동 슬립 카운트다운 라벨. 최상위 레이어에 두어 차트 위에 그린다.
       라벨은 기본적으로 클릭 대상이 아니라 터치를 가로채지 않는다. */
    po_label = lv_label_create(lv_layer_top());
    lv_label_set_text(po_label, "AUTO OFF IN 10");
    lv_obj_set_style_text_color(po_label, lv_color_hex(0xFF7A7A), LV_PART_MAIN);
    lv_obj_set_style_bg_color  (po_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa    (po_label, LV_OPA_80,              LV_PART_MAIN);
    lv_obj_set_style_pad_all   (po_label, 10,                     LV_PART_MAIN);
    lv_obj_set_style_radius    (po_label, 6,                      LV_PART_MAIN);
    lv_obj_align(po_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(po_label, LV_OBJ_FLAG_HIDDEN);

    apply_y_preset(0);

    lv_timer_create(draw_timer_cb, DRAW_MS, NULL);
}

void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 3000)) delay(10);
    delay(300);

    Serial.println();
    Serial.printf("[EQTool] boot  board=ESP32-C6-Touch-LCD-1.47  reset=%d  heap=%u\n",
                  (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
    Serial.println("[EQTool] serial: p=log  z=zero  i=info  a=accel  l=lcd test  o=power off");

    LCD_Init();        /* JD9853 초기화. 백라이트는 꺼둔 채로 준비만 한다 */
    Lvgl_Init();       /* LVGL + AXS5106L 터치 */
    ui_init();

#if ORIENT_ENABLE
    IMU_Init();        /* Wire 는 Touch_Init() 이 이미 열어두었다 */
#endif

    /* 배터리 표시를 먼저. Pressure 태스크가 돌기 시작하면 같은 ADC1 을
       1 kHz 로 쓰기 때문에, 그 전에 조용할 때 읽어둔다. */
    Battery_Splash();               /* 5초간 표시. 터치하면 건너뜀 */

    Pressure_Start();
    Chart_Init();

    /* 여기서 첫 프레임을 실제로 그린 다음 백라이트를 켠다.
       LCD_Init() 은 백라이트를 꺼둔 채로 준비만 해두었다.
       이렇게 하지 않으면 LVGL 이 처음 그릴 때까지 1~2초간
       검은 화면이 그대로 보인다. */
    lv_refr_now(NULL);
    Set_Backlight(LCD_BL_DEFAULT);

    Web_Start();

    activity_reset();               /* 자동 슬립 타이머 시작 */

    Serial.printf("[EQTool] setup done  heap=%u  auto-sleep=%us\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)(AUTO_SLEEP_MS / 1000));
}

void loop()
{
    Serial_Command();
    Timer_Loop();

#if ORIENT_ENABLE
    if (rot_req >= 0) {
        uint8_t r = (uint8_t)rot_req;
        rot_req = -1;
        Screen_SetRotation(r);
    }
#endif

    /* LVGL 이벤트 안에서 바로 자면 위험하므로 여기서 처리한다 */
    if (power_off_req) {
        power_off_req = false;
        Power_Off();
    }

    delay(1);
}
