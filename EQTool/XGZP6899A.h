/*****************************************************************************
 *  XGZP6899A  -  아날로그 전압 출력 압력 센서 (CFSensor)
 *
 *  칩 마킹 "6899 / 040A" =  6899 시리즈 / 40 kPa / A = Analog output
 *  I2C 가 아니다. 출력 핀의 전압을 ADC 로 읽는다.
 *
 *  ---- 배선 (SOP8) --------------------------------------------------------
 *    pin 2 (GND)  -- GND    (H1 3 / 4 / 6번)
 *    pin 5 (OUT)  -- GPIO3  (ADC1_CH3, H1 15번)  <- 여기가 유일한 신호선
 *    pin 6 (VDD)  -- 3V3    (H1 8번)
 *
 *  ※ ESP32-C6-Touch-LCD-1.47 의 ADC1 은 GPIO0~6 뿐이고 회로도상 전부 물려 있다.
 *     GPIO0 = BAT_ADC / GPIO1,2 = LCD SPI + TF / GPIO3 = TF MISO
 *     GPIO4 = TF CS  / GPIO5 = IMU_INT1 / GPIO6 = IMU_INT2
 *     GPIO5,6 은 QMI8658 IMU 가 능동으로 구동하므로 아날로그 입력으로 쓸 수 없다
 *     (센서 출력이 IMU 에 끌려 내려간다).
 *     따라서 TF 카드를 포기하고 그쪽 핀을 쓴다.
 *
 *  ※ GPIO4(TF CS) 가 아니라 GPIO3(TF MISO) 를 쓴다.
 *     GPIO4 는 ESP32-C6 의 JTAG MTMS 겸용이라 리셋 직후 내부 풀업이 걸릴 수 있어
 *     미연결 상태와 센서 출력을 구분하기 어렵다. GPIO3 은 그런 겸용 기능이 없다.
 *
 *  ---- OUT 핀을 찾는 법 (중요) --------------------------------------------
 *     SOP8 핀 번호를 세는 것보다 전압을 재는 쪽이 안전하다. 센서에 전원을
 *     넣은 상태에서 8개 리드를 하나씩 찍으면:
 *         3.3 V  -> VDD
 *         0 V    -> GND
 *         1~2 V  -> OUT      <- 이것
 *         불안정 -> N/C
 *     데이터시트 핀 테이블은 '윗면' 기준이라, 바닥에서 보고 번호를 세면
 *     좌우가 뒤집힌다. 번호를 세지 말고 전압으로 찾을 것.
 *
 *     OUT 자리가 레일(0 V 또는 VDD 근처)에 고착돼 있으면 센서 불량이다.
 *     실제로 같은 배치에서 2개 연속 DOA 를 겪었다 (각각 3.0 V / 0 V 고착).
 *     정상품은 무압에서 1.1 V 였다.
 ****************************************************************************/
#pragma once
#include <Arduino.h>

#define XGZP_ADC_PIN        3          /* OUT 을 물린 GPIO. H1 15번 (TF MISO 자리, TF 카드는 안 쓴다) */
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
