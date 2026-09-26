/*****************************************************************************
 *  XGZP6899A  -  아날로그 전압 출력 압력 센서 (CFSensor)
 *
 *  칩 마킹 "6899 / 040A" =  6899 시리즈 / 40 kPa / A = Analog output
 *  I2C 가 아니다. 출력 핀의 전압을 ADC 로 읽는다.
 *
 *  ---- 배선 (SOP8) --------------------------------------------------------
 *    pin 2 (GND)  -- GND
 *    pin 5 (OUT)  -- GPIO4   (ADC1_CH4)          <- 여기가 유일한 신호선
 *    pin 6 (VDD)  -- 3V3
 *
 *  ※ ESP32-C6-Touch-LCD-1.47 의 ADC1 은 GPIO0~6 뿐이고 회로도상 전부 물려 있다.
 *     GPIO0 = BAT_ADC / GPIO1,2 = LCD SPI + TF / GPIO3 = TF MISO
 *     GPIO4 = TF CS  / GPIO5 = IMU_INT1 / GPIO6 = IMU_INT2
 *     GPIO5,6 은 QMI8658 IMU 가 능동으로 구동하므로 아날로그 입력으로 쓸 수 없다
 *     (센서 출력이 IMU 에 끌려 내려간다).
 *     따라서 TF 카드를 포기하고 GPIO4 를 쓴다. 10K 풀업이 하나 붙어 있지만
 *     센서 출력단 임피던스가 낮아 오차는 무시할 수준이다.
 ****************************************************************************/
#pragma once
#include <Arduino.h>

#define XGZP_ADC_PIN        4          /* OUT 을 물린 GPIO (TF CS 자리. TF 카드는 안 쓴다) */
#define XGZP_VDD_MV         3300.0f    /* 실제 공급 전압(mV). 정확할수록 좋다 */
#define XGZP_FS_KPA         40.0f      /* 풀스케일 (마킹 040 = 40 kPa) */
#define XGZP_SPAN_RATIO     0.80f      /* 출력 스팬 = 10%~90% VDD */
#define XGZP_BIDIRECTIONAL  1          /* 1 = -40~+40 (DPN), 0 = 0~40 (DG) */
#define XGZP_AUTO_DETECT    1          /* 1 = 무압 출력 전압으로 DPN/DG 자동 판별 */
#define XGZP_OVERSAMPLE      16          /* 한 샘플당 ADC 평균 횟수 (늘리면 노이즈 감소, 속도 감소) */

bool  XGZPA_Init(void);
float XGZPA_ReadMv(void);              /* 출력 전압 (mV), 오버샘플 평균 */
float XGZPA_MvToPa(float mv);          /* 전압 -> Pa */
void  XGZPA_AutoZero(uint32_t ms);     /* 무압 상태 기준점 잡기 */
float XGZPA_GetZeroMv(void);
void  XGZPA_SetZeroMv(float mv);
float XGZPA_SensMvPerKpa(void);
bool  XGZPA_IsBidirectional(void);
