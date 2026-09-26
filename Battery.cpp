#include "Battery.h"

float Battery_ReadMv(void)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < BAT_OVERSAMPLE; i++)
        sum += analogReadMilliVolts(BAT_ADC_PIN);

    return ((float)sum / BAT_OVERSAMPLE) * BAT_DIV_RATIO;
}

/* 1셀 LiPo 방전 곡선.
   선형 보간(3.3V=0%, 4.2V=100%)은 중간 구간이 실제와 크게 어긋나서
   구간별 표를 쓴다. 값은 무부하 기준이라 방전 중에는 조금 낮게 나온다. */
uint8_t Battery_Percent(float mv)
{
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
        { 4200, 100 }, { 4100,  90 }, { 4000,  80 }, { 3950,  70 },
        { 3880,  60 }, { 3840,  50 }, { 3800,  40 }, { 3760,  30 },
        { 3730,  20 }, { 3700,  15 }, { 3650,  10 }, { 3500,   5 },
        { 3300,   0 },
    };
    const uint8_t n = sizeof(curve) / sizeof(curve[0]);

    if (mv >= curve[0].mv)     return 100;
    if (mv <= curve[n-1].mv)   return 0;

    for (uint8_t i = 1; i < n; i++) {
        if (mv >= curve[i].mv) {
            /* curve[i] ~ curve[i-1] 사이를 선형 보간 */
            float span = (float)(curve[i-1].mv - curve[i].mv);
            float frac = (mv - curve[i].mv) / span;
            return (uint8_t)(curve[i].pct + frac * (curve[i-1].pct - curve[i].pct) + 0.5f);
        }
    }
    return 0;
}
