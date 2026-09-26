/*****************************************************************************
 *  IMU_Orient.h  -  QMI8658 가속도 읽기 (화면 자동 회전용)
 *
 *  ESP32-C6-Touch-LCD-1.47 의 IMU 는 터치와 같은 I2C 버스에 있다.
 *      SDA=18  SCL=19  주소 0x6B
 *      INT1=GPIO5, INT2=GPIO6 (여기서는 안 쓴다. 폴링만 한다)
 *
 *  Wire.begin() 은 Touch_Init() 이 이미 해두므로 여기서는 하지 않는다.
 *  따라서 IMU_Init() 은 반드시 Touch_Init() 뒤에 불러야 한다.
 ****************************************************************************/
#pragma once

#include <Arduino.h>

#define IMU_I2C_ADDR   0x6B

bool IMU_Init(void);

/* 가속도 (단위: g). 실패하면 false.
   IMU 가 없거나 초기화 실패면 항상 false 를 돌려주므로,
   호출부는 화면 회전을 그냥 건너뛰면 된다. */
bool IMU_ReadAccel(float *ax, float *ay, float *az);

bool IMU_IsOk(void);
