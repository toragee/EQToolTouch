# EQToolTouch

ESP32-C6 기반 차압(differential pressure) 측정기.
Waveshare **ESP32-C6-Touch-LCD-1.47** 보드에 **XGZP6899A** 아날로그 압력
센서를 붙여, LVGL 차트로 실시간 표시하고 WiFi AP + 웹소켓으로
PC / 안드로이드에 스트리밍한다.

## 저장소 구조

```
EQTool/          아두이노 스케치 (펌웨어)  <- 여기가 본체
  README.md      핀맵, 전원 관리, 문제 해결 기록. 먼저 읽을 것
case/            케이스 설계
  stl/           출력용 STL
  XGZP6899A.step 센서 3D 모델 (데이터시트 도면으로 생성)
  ESP32-C6-Touch-LCD-1.47-2D3D/   보드 2D/3D (Waveshare 배포본)
```

## 브랜치

센서 하드웨어 리비전에 따라 갈라진다.

| 브랜치 | 센서 | 상태 |
|---|---|---|
| `sensor-analog` | **XGZP6899A** 040KPDPN (아날로그, ±40 kPa) | 동작 확인됨. 고정 |
| `main` | 현행 개발 라인 | |

`XGZP6899` 바로 뒤 한 글자가 출력 방식이다. **`A` = 아날로그, `D` = I2C.**
`6899A040KPDPN` 은 아날로그, `6899D040KPDPN` 이 I2C 다.
수급 문제로 센서를 바꿀 때 이 글자를 반드시 확인할 것.

### 아날로그 버전(`sensor-analog`)의 특징

- 출력이 ADC 한 핀(**GPIO4**)뿐이라 **약 980 Hz** 로 샘플링한다
- 부팅 시 0.5초 평균으로 영점을 잡고, 그 전압이 VDD 의 몇 %인지로
  DPN(양방향) / DG(단방향)를 자동 판별한다

> **I2C 버전으로 가면 샘플레이트를 유지할 수 없다.** 한 샘플마다
> 변환 명령 → 대기 → 3바이트 읽기가 필요해서 현실적으로 100 Hz 이하가 된다.
> 터치(0x63)·IMU(0x6B)와 버스를 공유하는 부담도 있다.
> 빠른 압력 변화를 봐야 한다면 아날로그 쪽이 유리하다.

## 빠른 시작

1. 아두이노 IDE 2.x, 보드 패키지 **esp32 by Espressif**
2. Waveshare 위키에서 `ESP32-C6-Touch-LCD-1.47-Demo` 를 받아
   그 안의 `Arduino/libraries` 를 스케치북 라이브러리로 설치
   (lvgl 8.4.0 / GFX_Library_for_Arduino 1.5.9 / esp_lcd_touch_axs5106l / FastIMU)
3. `EQTool/EQTool.ino` 를 열고 Tools 설정을 맞춘 뒤 업로드

> **용량이 커서 벤더 데모(155MB)와 zip 파일은 저장소에 포함하지 않았다.**
> 버전이 맞지 않으면 빌드가 조용히 깨지므로, 요구 버전은
> `EQTool/README.md` 의 "빌드" 항목을 그대로 따를 것.

자세한 내용(핀맵, 조작, 전원 관리, 그동안 겪은 문제와 해결)은
**[EQTool/README.md](EQTool/README.md)** 에 정리되어 있다.
