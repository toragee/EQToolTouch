#include "Battery.h"

float Battery_ReadMv(void)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < BAT_OVERSAMPLE; i++)
        sum += analogReadMilliVolts(BAT_ADC_PIN);

    return ((float)sum / BAT_OVERSAMPLE) * BAT_DIV_RATIO * BAT_CAL;
}

/* 잔량 곡선 — 200 mAh 셀을 실제로 구동하면서 측정한 기준점.
 *
 *      4.00 V -> 100 %
 *      3.70 V ->  50 %
 *      3.20 V ->   0 %
 *
 *  데이터시트의 무부하(OCV) 곡선이 아니라 **부하가 걸린 상태**의 값이다.
 *  200 mAh 소용량 셀이라 내부저항이 커서, 동작 중에는 무부하보다
 *  0.1 V 가량 낮게 측정된다. 그래서 만충 4.2 V 셀이 동작 중에는
 *  4.0 V 근처로 읽히고, 그 지점을 100 % 로 잡았다.
 *
 *  즉 이 표는 "이 기기에서 이 셀을 쓸 때" 의 실측 스케일이다.
 *  셀을 다른 용량/제조사로 바꾸면 다시 잡아야 한다.
 *
 *  구간 사이는 선형 보간한다. 리튬의 실제 방전 곡선은 평탄하다가
 *  끝에서 꺾이지만, 3점 직선이 체감과 더 잘 맞는다는 판단.
 */
uint8_t Battery_Percent(float mv)
{
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
        { 4000, 100 },
        { 3700,  50 },
        { 3200,   0 },
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
