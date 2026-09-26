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
