#include "XGZP6899A.h"

static float zero_mv = -1.0f;          /* AutoZero 전에는 이론값을 쓴다 */
static bool  bidir   = (XGZP_BIDIRECTIONAL != 0);

float XGZPA_SensMvPerKpa(void)
{
    /* 양방향(DPN): -FS ~ +FS 가 스팬 전체에 대응 -> 감도가 절반 */
    return bidir ? (XGZP_SPAN_RATIO * XGZP_VDD_MV / (2.0f * XGZP_FS_KPA))
                 : (XGZP_SPAN_RATIO * XGZP_VDD_MV / XGZP_FS_KPA);
}

bool XGZPA_IsBidirectional(void) { return bidir; }

/* 이론적 무압 출력: 양방향이면 50% VDD, 단방향이면 10% VDD */
static float theoretical_zero_mv(void)
{
    return bidir ? (XGZP_VDD_MV * 0.50f)
                 : (XGZP_VDD_MV * ((1.0f - XGZP_SPAN_RATIO) / 2.0f));
}

bool XGZPA_Init(void)
{
    static int8_t last_state = -2;      /* -2=최초, -1=실패, 0=성공 */
    int8_t        state;

    analogReadResolution(12);
    analogSetPinAttenuation(XGZP_ADC_PIN, ADC_11db);   /* 0 ~ 약 3.1V */
    delay(10);

    if (zero_mv < 0.0f) zero_mv = theoretical_zero_mv();

    float mv = XGZPA_ReadMv();

    if      (mv < 50.0f)                  state = -1;   /* OUT 미연결 / VDD 없음 */
    else if (mv > XGZP_VDD_MV - 100.0f)   state = -1;   /* VDD 에 단락 / 핀 오인 */
    else                                  state =  0;

    /* 상태가 바뀔 때만 출력한다 (재시도 루프에서 매초 도배되지 않도록) */
    if (state != last_state) {
        last_state = state;
        if (state == 0) {
            Serial.printf("[XGZP] analog OUT=GPIO%d  %.1f mV  sens %.2f mV/kPa  (+-%.0f kPa)\n",
                          XGZP_ADC_PIN, mv, XGZPA_SensMvPerKpa(), XGZP_FS_KPA);
        } else {
            Serial.printf("[XGZP] ERROR: OUT=GPIO%d reads %.1f mV - %s\n",
                          XGZP_ADC_PIN, mv,
                          (mv < 50.0f) ? "OUT not connected or VDD missing"
                                       : "OUT shorted to VDD or wrong pin");
        }
    }
    return (state == 0);
}

float XGZPA_ReadMv(void)
{
    uint32_t acc = 0;
    for (int i = 0; i < XGZP_OVERSAMPLE; i++)
        acc += analogReadMilliVolts(XGZP_ADC_PIN);
    return (float)acc / (float)XGZP_OVERSAMPLE;
}

float XGZPA_MvToPa(float mv)
{
    return (mv - zero_mv) / XGZPA_SensMvPerKpa() * 1000.0f;   /* kPa -> Pa */
}

/* 양쪽 포트를 개방한 상태에서 호출할 것. 그때의 출력을 0 Pa 로 잡는다. */
void XGZPA_AutoZero(uint32_t ms)
{
    double   acc = 0.0;
    uint32_t n   = 0;
    uint32_t t0  = millis();

    while (millis() - t0 < ms) {
        acc += XGZPA_ReadMv();
        n++;
        delay(2);
    }
    if (n == 0) return;

    zero_mv = (float)(acc / n);

#if XGZP_AUTO_DETECT
    float pct = zero_mv / XGZP_VDD_MV * 100.0f;
    bidir = (pct > 30.0f);

    if (pct < 3.0f || (pct > 20.0f && pct < 40.0f) || pct > 65.0f)
        Serial.printf("[XGZP] WARNING: zero at %.0f%% of VDD is unexpected - "
                      "wrong supply model, wrong pin, or pressure applied\n", pct);
#endif

    Serial.printf("[XGZP] zero %.1f mV (%.0f%% VDD, %s)  sens %.2f mV/kPa\n",
                  zero_mv, zero_mv / XGZP_VDD_MV * 100.0f,
                  bidir ? "DPN" : "DG", XGZPA_SensMvPerKpa());
}

float XGZPA_GetZeroMv(void)        { return zero_mv; }
void  XGZPA_SetZeroMv(float mv)    { zero_mv = mv; }
