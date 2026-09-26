/*****************************************************************************
 *  Pressure_Sampler
 *  XGZP6899A(아날로그 출력)를 별도 FreeRTOS 태스크에서 주기적으로 ADC 로 읽어
 *  링버퍼에 쌓는다. UI(LVGL) 쪽은 링버퍼에서 꺼내 쓰기만 한다.
 *****************************************************************************/
#pragma once
#include <Arduino.h>

bool     Pressure_Start(void);        /* 센서 확인 + 오토제로 + 태스크 생성 */

bool     Pressure_Pop(float *pa);     /* LVGL 차트용 소비자 */
bool     Pressure_PopWeb(float *pa);  /* 웹 송신용 소비자 (독립 tail) */
uint32_t Pressure_GetRateHz(void);    /* 최근 1초간 실측 샘플링 주파수 */
uint32_t Pressure_GetDropped(void);   /* 링버퍼 오버런으로 버린 샘플 수 */
float    Pressure_GetLastPa(void);
float    Pressure_GetFilteredPa(void);  /* 표시용 저역통과 적용값 */
float    Pressure_GetLastMv(void);
bool     Pressure_IsOk(void);
void     Pressure_Rezero(void);       /* 런타임 중 영점 재설정 */

/* 1차 IIR 필터. 0 Hz 를 주면 해당 단이 꺼진다.
 *   저역통과 : 노이즈 제거 (숫자가 작을수록 강함)
 *   고역통과 : DC/드리프트 제거 (숫자가 클수록 강함) */
void     Pressure_SetLowPass(float hz);
void     Pressure_SetHighPass(float hz);
float    Pressure_GetLowPass(void);
float    Pressure_GetHighPass(void);
