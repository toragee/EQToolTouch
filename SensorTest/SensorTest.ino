/*****************************************************************************
 *  SensorTest  -  XGZP6899 센서 진단 (ESP32-C6-Touch-LCD-1.47)
 *
 *  이 스케치 하나로 두 가지를 동시에 본다.
 *    1) I2C 버스를 스캔해서 센서(0x6D)가 응답하는지  -> D(디지털) 버전인가
 *    2) GPIO4 의 아날로그 전압이 압력에 반응하는지    -> A(아날로그) 버전인가
 *
 *  ---- 배선 --------------------------------------------------------------
 *  공통            : 센서 2번(GND) -> GND,  6번(VDD) -> 3V3
 *
 *  아날로그 확인용 : 센서 5번(OUT) -> GPIO4
 *  I2C 확인용      : 센서 3번(SCL) -> 헤더 실크 "SCL" (H1 10번 = GPIO19)
 *                    센서 4번(SDA) -> 헤더 실크 "SDA" (H1 12번 = GPIO18)
 *
 *  ※ 보드 헤더에는 GPIO 번호 대신 신호 이름으로 실크가 찍혀 있다.
 *    SDA/SCL 실크가 그대로 GPIO18/19 다.
 *  ※ 세 선을 다 연결해도 된다. A 버전이면 3/4번이 N/C 라 무해하고,
 *    D 버전이면 5번이 N/C 라 역시 무해하다. 한 번에 판별할 수 있다.
 *  ※ 보드에 10K 풀업(R11/R12)이 이미 있으니 I2C 풀업은 따로 안 붙여도 된다.
 *  ※ 이 버스에는 터치(0x63)와 IMU(0x6B)가 이미 붙어 있다. 스캔에서 이 둘이
 *    안 보이면 센서가 아니라 SDA/SCL 배선이 잘못된 것이다.
 *
 *  ---- 보는 법 ------------------------------------------------------------
 *  포트에 입으로 약하게 불었을 때
 *    - ANALOG 의 mV 가 움직이면      -> 아날로그 센서가 살아 있다
 *    - I2C 의 raw 값이 움직이면      -> I2C 센서가 살아 있다
 *  둘 다 안 움직이면 배선이나 전원을 먼저 의심할 것.
 *
 *  ---- 자동 스캔 -----------------------------------------------------------
 *  1초마다 I2C 버스를 전체 스캔한다. 오실로스코프/로직분석기로 SCL·SDA 에
 *  프로브를 대고 있으면 1초 주기로 버스트가 계속 보인다. 키를 누를 필요가 없다.
 *  출력은 조용하다 - 발견된 주소 목록이 '바뀔 때만' 찍는다. 그래서 배선을
 *  살살 흔들다가 센서가 붙었다/떨어졌다 하면 그 순간 로그에 남는다
 *  (접촉 불량 추적용).
 *
 *  시리얼 115200.
 *    's' = 지금 바로 재스캔(결과 강제 출력)
 *    'a' = 자동 스캔 ON/OFF
 *    'z' = 아날로그 기준점 다시 잡기
 ****************************************************************************/
#include <Arduino.h>
#include <Wire.h>

/* ---- 핀 / 버스 ---- */
#define I2C_SDA        18
#define I2C_SCL        19
#define I2C_HZ         100000      /* 테스트라 느리게. 안정성 우선 */
#define ANALOG_PIN_A   3          /* 1순위: GPIO3 = H1 15번 (IO3). 보드에서 안 쓰는 핀 */
#define ANALOG_PIN_B   4          /* 2순위: GPIO4 = H1 17번 (IO4). JTAG MTMS 겸용 */

/* ---- 아날로그 변환 (XGZP6899A, ±40 kPa, DPN 가정) ---- */
#define VDD_MV         3300.0f
#define FS_KPA         40.0f
#define SPAN_RATIO     0.80f       /* 출력 스팬 10%~90% VDD */
#define OVERSAMPLE     16

/* ---- I2C 센서 (XGZP6899D) ---- */
#define XGZP_ADDR      0x6D
#define REG_CMD        0x30
#define CMD_CONVERT    0x0A
#define REG_PRESS_MSB  0x06        /* 0x06,0x07,0x08 = 24bit */
#define REG_TEMP_MSB   0x09        /* 0x09,0x0A       = 16bit */

/*  K 값은 측정 범위에 따라 다르다. 데이터시트 표를 확인할 것.
 *  40 kPa 품이면 보통 512 (결과 단위 Pa).
 *  숫자가 터무니없으면 이 값을 바꿔보면 된다. 어차피 이 스케치의 목적은
 *  "데이터가 들어오는가" 이지 정밀도가 아니다. raw 값이 더 중요하다. */
#define XGZP_K         512.0f

/* ---- 자동 스캔 ---- */
#define AUTOSCAN_MS    1000       /* I2C 전체 스캔 주기 (ms) */

static bool     i2c_sensor_found = false;
static float    base_a           = 0.0f;
static float    base_b           = 0.0f;
static bool     autoscan         = true;
static uint32_t scan_count       = 0;

/* ===================== I2C 유틸 ========================================= */
static bool reg_write8(uint8_t addr, uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool reg_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;      /* repeated start */
    if (Wire.requestFrom(addr, n) != n)   return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

/*  발견된 주소를 비트마스크로 기억한다.
 *  verbose=true  -> 무조건 전체 출력 (부팅 시, 's' 키)
 *  verbose=false -> 목록이 '바뀔 때만' 출력 (자동 스캔)
 *  어느 쪽이든 버스에는 똑같이 전체 스캔 트래픽이 흐른다. */
static uint32_t seen_mask[4] = { 0, 0, 0, 0 };

static inline bool mask_get(const uint32_t *m, uint8_t a)
{
    return (m[a >> 5] >> (a & 31)) & 1U;
}

static void i2c_scan(bool verbose)
{
    uint32_t mask[4] = { 0, 0, 0, 0 };
    uint8_t  found   = 0;
    bool     sensor  = false;

    for (uint8_t a = 0x00; a < 0x80; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() != 0) continue;

        found++;
        mask[a >> 5] |= (1UL << (a & 31));
        if (a == XGZP_ADDR) sensor = true;
    }

    bool changed = (memcmp(mask, seen_mask, sizeof(mask)) != 0);
    memcpy(seen_mask, mask, sizeof(mask));
    i2c_sensor_found = sensor;

    if (!verbose && !changed) return;        /* 조용히 통과 */

    Serial.printf("\n[I2C] 스캔 0x00~0x7F (SDA=%d, SCL=%d)%s\n", I2C_SDA, I2C_SCL,
                  (changed && !verbose) ? "   *** 변화 감지 ***" : "");

    for (uint8_t a = 0x00; a < 0x80; a++) {
        if (!mask_get(mask, a)) continue;
        const char *who = "알 수 없음";
        if      (a == 0x63)      who = "AXS5106L 터치 (보드 내장)";
        else if (a == 0x6B)      who = "QMI8658 IMU (보드 내장)";
        else if (a == XGZP_ADDR) who = "XGZP6899D 압력센서  <<< I2C 센서!";
        Serial.printf("  0x%02X  %s\n", a, who);
    }

    if (!found) Serial.println("  (응답 없음 - 버스가 죽었거나 배선 문제)");
    Serial.printf("[I2C] %u개 발견.  압력센서(0x%02X): %s\n\n",
                  found, XGZP_ADDR, sensor ? "있음" : "없음");
}

/* ===================== XGZP6899D 읽기 =================================== */
static bool xgzp_read(int32_t *raw_out, float *pa_out, float *tc_out)
{
    if (!reg_write8(XGZP_ADDR, REG_CMD, CMD_CONVERT)) return false;

    /* 변환 완료 대기. Sco(bit3)가 0 이 되면 끝. */
    uint8_t  st = 0;
    uint32_t t0 = millis();
    do {
        delay(2);
        if (!reg_read(XGZP_ADDR, REG_CMD, &st, 1)) return false;
    } while ((st & 0x08) && (millis() - t0 < 100));
    if (st & 0x08) return false;                 /* 타임아웃 */

    uint8_t b[5];
    if (!reg_read(XGZP_ADDR, REG_PRESS_MSB, b, 5)) return false;

    int32_t raw = ((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2];
    if (raw & 0x800000) raw -= 0x1000000;        /* 음압 = 2의 보수 */

    int16_t traw = (int16_t)(((uint16_t)b[3] << 8) | b[4]);

    if (raw_out) *raw_out = raw;
    if (pa_out)  *pa_out  = raw / XGZP_K;
    if (tc_out)  *tc_out  = traw / 256.0f;
    return true;
}

/* ===================== 아날로그 읽기 ==================================== */
static float analog_mv(uint8_t pin)
{
    pinMode(pin, ANALOG);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < OVERSAMPLE; i++) sum += analogReadMilliVolts(pin);
    return (float)sum / OVERSAMPLE;
}

/* DPN(양방향) 가정. 기준점(무압) 대비 변화량으로 환산한다. */
static float analog_kpa(float mv, float base)
{
    float sens = SPAN_RATIO * VDD_MV / (2.0f * FS_KPA);   /* mV per kPa */
    return (mv - base) / sens;
}

/* 레일/플로팅/구동 중 어느 상태인지 한 단어로 */
static const char *verdict(float mv)
{
    if (mv > VDD_MV * 0.95f) return "레일(미연결?)";
    if (mv < VDD_MV * 0.03f) return "0V(GND?)";
    return "구동중";
}

/* ===================== setup / loop ===================================== */
void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    delay(300);

    Serial.println("\n\n=======================================");
    Serial.println(" XGZP6899 센서 진단");
    Serial.println("=======================================");
    Serial.println(" 배선: 2=GND  6=3V3");
    Serial.printf (" 아날로그 후보: A=GPIO%d (H1 15번)  B=GPIO%d (H1 17번)\n",
                   ANALOG_PIN_A, ANALOG_PIN_B);
    Serial.println(" 센서 OUT 을 둘 중 아무 쪽에 꽂아도 됩니다. 둘 다 읽습니다.");
    Serial.println("       3=실크 SCL  4=실크 SDA  (= GPIO19/18)");
    Serial.println(" 명령: s=재스캔  a=자동스캔 ON/OFF  z=기준점 재설정");
    Serial.printf (" 자동 스캔: %d ms 주기 (기본 ON)\n", AUTOSCAN_MS);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_HZ);
    i2c_scan(true);

    base_a = analog_mv(ANALOG_PIN_A);
    base_b = analog_mv(ANALOG_PIN_B);
    Serial.printf("[ANALOG] 기준점  A(GPIO%d)=%.1f mV %s   B(GPIO%d)=%.1f mV %s\n",
                  ANALOG_PIN_A, base_a, verdict(base_a),
                  ANALOG_PIN_B, base_b, verdict(base_b));

    Serial.println("\n이제 포트에 약하게 불어보세요. 값이 움직이면 정상입니다.\n");
}

void loop()
{
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 's' || c == 'S') i2c_scan(true);
        if (c == 'a' || c == 'A') {
            autoscan = !autoscan;
            Serial.printf("[I2C] 자동 스캔 %s (%d ms 주기)\n",
                          autoscan ? "ON" : "OFF", AUTOSCAN_MS);
        }
        if (c == 'z' || c == 'Z') {
            base_a = analog_mv(ANALOG_PIN_A);
            base_b = analog_mv(ANALOG_PIN_B);
            Serial.printf("[ANALOG] 기준점 재설정 -> A=%.1f mV  B=%.1f mV\n",
                          base_a, base_b);
        }
    }

    /* --- 1초마다 I2C 전체 스캔. 스코프 프로브용 주기적 버스트 --- */
    static uint32_t t_scan = 0;
    if (autoscan && (millis() - t_scan >= AUTOSCAN_MS)) {
        t_scan = millis();
        scan_count++;
       // i2c_scan(false);
    }

    /* --- 0.5초마다 아날로그 상태 한 줄 --- */
    static uint32_t t_print = 0;
    if (millis() - t_print >= 500) {
        t_print = millis();

        float a = analog_mv(ANALOG_PIN_A);
        float b = analog_mv(ANALOG_PIN_B);
        Serial.printf("A(IO%d) %7.1f mV %+7.1f  %+6.2f kPa  |  B(IO%d) %7.1f mV %+7.1f  %+6.2f kPa  [scan %lu]",
                      ANALOG_PIN_A, a, a - base_a, analog_kpa(a, base_a),
                      ANALOG_PIN_B, b, b - base_b, analog_kpa(b, base_b),
                      (unsigned long)scan_count);

        if (i2c_sensor_found) {
            int32_t raw; float pa, tc;
            if (xgzp_read(&raw, &pa, &tc))
                Serial.printf("   |  I2C  raw=%8ld  %+9.1f Pa  %.1fC", (long)raw, pa, tc);
            else
                Serial.print("   |  I2C  읽기 실패");
        }
        Serial.println();
    }
}
