#include "IMU_Orient.h"
#include <FastIMU.h>

static QMI8658  imu;
static calData  calib = { 0 };      /* 방향 판별용이라 캘리브레이션은 생략 */
static bool     imu_ok = false;

bool IMU_Init(void)
{
    /* Wire 는 Touch_Init() 에서 이미 begin 되어 있다 */
    int err = imu.init(calib, IMU_I2C_ADDR);
    imu_ok = (err == 0);

    if (imu_ok) Serial.printf("[IMU] QMI8658 ok  (0x%02X)\n", IMU_I2C_ADDR);
    else        Serial.printf("[IMU] QMI8658 init failed (err=%d) - 화면 자동회전 비활성\n", err);

    return imu_ok;
}

bool IMU_IsOk(void) { return imu_ok; }

bool IMU_ReadAccel(float *ax, float *ay, float *az)
{
    if (!imu_ok) return false;

    AccelData a;
    imu.update();
    imu.getAccel(&a);

    if (ax) *ax = a.accelX;
    if (ay) *ay = a.accelY;
    if (az) *az = a.accelZ;
    return true;
}
