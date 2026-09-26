/*****************************************************************************
 *  Battery.h  -  ESP32-C6-Touch-LCD-1.47 배터리 전압 읽기
 *
 *  회로도 (BAT 블록):
 *      VBAT --[ R21 200K ]--+--[ R22 100K ]-- GND
 *                           |
 *                        BAT_ADC (GPIO0)
 *
 *  분압비 = (200K + 100K) / 100K = 3
 *  => VBAT = ADC 읽은 전압 x 3
 *
 *  1셀 LiPo 만충 4.2V -> ADC 에는 1.4V 가 걸리므로 C6 ADC 범위에 여유가 있다.
 *
 *  ※ USB 가 꽂혀 있으면 충전 IC(ETA6098)가 전압을 끌어올리기 때문에
 *    이 값은 "배터리 잔량" 이 아니라 "충전 전압" 이다. 잔량으로 쓰지 말 것.
 ****************************************************************************/
#pragma once

#include <Arduino.h>

#define BAT_ADC_PIN     0           /* 회로도 BAT_ADC */
#define BAT_DIV_RATIO   3.0f        /* (200K + 100K) / 100K */
#define BAT_OVERSAMPLE  16          /* 평균 횟수 (노이즈 억제) */

/* 배터리 전압 (mV). 분압 보정까지 끝난 값. */
float Battery_ReadMv(void);

/* 전압 -> 잔량 % (0~100). 1셀 LiPo 방전 곡선 기준. */
uint8_t Battery_Percent(float mv);
