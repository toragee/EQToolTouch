# EQTool

ESP32-C6 기반 압력 측정 도구. Waveshare **ESP32-C6-Touch-LCD-1.47** 보드에서
**XGZP6899A** 아날로그 압력 센서를 읽어 LVGL 차트로 실시간 표시하고,
WiFi AP + 내장 웹서버로 PC/안드로이드에 실시간 스트리밍한다.

## 하드웨어

| 항목 | 내용 |
|---|---|
| MCU 보드 | Waveshare **ESP32-C6-Touch-LCD-1.47** (JD9853 172x320 IPS + AXS5106L 터치) |
| 센서 | CFSensor XGZP6899A (40 kPa, 아날로그 전압 출력) |
| 화면 방향 | 가로 (320 x 172), `LCD_ROTATION 3` (Arduino_GFX 기준) |

### 보드 핀맵 (ESP32-C6-Touch-LCD-1.47)

| 기능 | 핀 |
|---|---|
| LCD (JD9853) | SCK=1, MOSI=2, CS=14, DC=15, RST=22, BL=23 |
| TF 카드 | SCK=1, MOSI=2, MISO=3, CS=4 (SPI 공유) |
| 터치 / IMU (I2C) | SDA=18, SCL=19, TP_RST=20, TP_INT=21 |
| 배터리 전압 | GPIO0 (ADC) |
| BOOT 버튼 | GPIO9 (Y축 전환. 화면 터치와 동일 동작) |
| IMU (QMI8658) INT | **GPIO5 = IMU_INT1, GPIO6 = IMU_INT2** |
| **압력 센서 OUT** | **GPIO4** (TF CS 자리) |

> ESP32-C6 의 ADC1 은 GPIO0~6 뿐인데 회로도상 전부 물려 있다.
> GPIO5/GPIO6 은 QMI8658 IMU 가 **능동으로 구동**하므로 아날로그 입력으로 못 쓴다.
> (붙여 보면 센서 출력이 IMU 에 끌려 내려가고 `adc oneshot read fail` 이 뜬다.)
> 남는 선택지는 **GPIO4** 뿐 — TF 카드를 포기하는 대신 쓴다. 10K 풀업이 하나
> 붙어 있지만 센서 출력단 임피던스가 낮아 오차는 무시할 수준이다.
> 데모 코드만 보고 GPIO5/6 이 비었다고 추정하면 안 된다. **회로도를 볼 것.**

### 배선

| 센서 핀 | 신호 | ESP32-C6 |
|---|---|---|
| 2 | GND | GND |
| 5 | OUT | **GPIO4** (ADC1_CH4) |
| 6 | VDD | 3V3 |
| 1, 3, 4, 7, 8 | N/C | 연결하지 않음 |

> XGZP6899**A** 는 아날로그 출력이다. I2C 모델(XGZP6899**D**)과 핀 배치가 다르다.
> 칩 마킹 `6899 / 040A` 에서 `A` 가 Analog, `040` 이 40 kPa 를 뜻한다.

## 소프트웨어 구성

| 파일 | 역할 |
|---|---|
| `EQTool.ino` | 진입점, LVGL 차트 설정, 시리얼 명령 |
| `XGZP6899A.h/.cpp` | 센서 드라이버 (ADC 읽기, 전압→압력 변환, 오토제로) |
| `Pressure_Sampler.h/.cpp` | FreeRTOS 샘플링 태스크 + 락 없는 링버퍼 |
| `Display_LCD.h/.cpp` | LCD 드라이버 — JD9853 초기화 + Arduino_GFX |
| `Touch_AXS5106.h/.cpp` | 정전식 터치 (I2C 0x63) 래퍼 |
| `LVGL_Driver.h/.cpp` | LVGL 연결 (flush / 터치 입력 / 드로우 버퍼) |
| `Battery.h/.cpp` | 배터리 전압 읽기 (BAT_ADC=GPIO0, 200K/100K 분압) |
| `IMU_Orient.h/.cpp` | QMI8658 가속도 읽기 (화면 자동 회전용) |
| `Web_Server.h/.cpp`, `web_page.h` | WiFi AP + WebSocket 스트리밍, 내장 웹 UI |
| `Display_ST7789.h/.cpp` | (구) ST7789 드라이버 — 비워둔 껍데기. 삭제해도 된다 |
| `lv_conf.h` | LVGL 설정 (데모 것 기반, `LV_COLOR_16_SWAP 1`) |
| `ui*.c/.h` | SquareLine Studio 1.6.2 export (LVGL 8.3) |

센서 샘플링(약 800 Hz)은 별도 태스크에서 돌고, UI 는 25 fps 로 링버퍼를 비우며
샘플레이트에 맞춰 자동으로 압축(decimation)한다.

### Y축 프리셋

```c
static const struct { int16_t min, max; uint8_t ticks; const char *name; } Y_PRESETS[] = {
    { -5000, 20000, 6, "-5 ~ 20 kPa" },   /* 눈금 -5/0/5/10/15/20 */
    { -5000, 15000, 5, "-5 ~ 15 kPa" },   /* 눈금 -5/0/5/10/15    */
    { -2000,  8000, 6, "-2 ~ 8 kPa"  },   /* 눈금 -2/0/2/4/6/8    */
};
```

**눈금 간격이 정수 kPa 로 떨어지게 잡아야 한다.** 간격은
`(max - min) / (ticks - 1)` 이다. 예전에 쓰던 `-3 ~ 10 / 6눈금` 은
간격이 2.6 kPa 라 `-3.0 / -0.4 / 2.2 / 4.8 / 7.4 / 10.0` 이 나와서 보기 나빴다.

0 이 눈금선에 오려면 `(0 - min)` 이 간격의 배수여야 한다.
세 프리셋 모두 0 이 아래에서 20~25 % 지점에 오도록 잡았다
(위쪽을 넓게 쓰려는 의도).

Y축 라벨은 그리기 이벤트에서 kPa 로 바꿔 그리며, **소수 자리가 0 이면
정수로만** 표시한다 (`0.0` 대신 `0`).

## 측정 원리

비율식(ratiometric) 아날로그 출력을 ADC 로 읽는다.

```
sens  = 0.80 x VDD / (2 x FS)          # 양방향(DPN) 기준
P(Pa) = (out_mV - zero_mV) / sens x 1000
```

부팅 시 0.5 초간 평균을 내어 영점을 잡고, 그 전압이 VDD 의 몇 % 인지로
DPN(양방향) / DG(단방향) 를 자동 판별한다.

## 빌드

- Arduino IDE 2.x
- 보드: **ESP32C6 Dev Module** (esp32 코어 3.x)
- 라이브러리 (Waveshare 데모 `ESP32-C6-Touch-LCD-1.47-Demo/Arduino/libraries/` 에서 복사)
  - **lvgl 8.3.x**
  - **GFX_Library_for_Arduino** (Arduino_GFX)
  - **esp_lcd_touch_axs5106l**
- `lv_conf.h` 는 스케치 폴더에 같이 들어 있다. lvgl 8.3 은 `__has_include("lv_conf.h")`
  로 스케치 폴더 것을 먼저 찾으므로 `libraries/lv_conf.h` 는 없어도 된다.
  (둘 다 있으면 스케치 폴더 것이 이긴다)
- 스케치북 경로에 **한글/공백이 없어야 한다**. (GCC 상대 include 가 깨진다)
- Tools 설정
  - USB CDC On Boot: **Enabled**
  - CPU Frequency: 160MHz (WiFi)
  - JTAG Adapter: Disabled
  - Flash Size: 4MB, Partition: Default 4MB with spiffs

## 조작

| 동작 | 결과 |
|---|---|
| **화면 짧게 터치** | Y축 스케일 프리셋 순환 (-5~20 → -5~15 → -2~8 kPa) |
| **BOOT 버튼(GPIO9)** | 위와 동일 |
| **꺼진 상태에서 화면 터치** | 다시 켜짐 (깨어난 뒤 재부팅) |
| **1분간 압력 변화 없음** | 자동으로 슬립. 마지막 10초는 `AUTO OFF IN n` 표시 |
| **기기를 뒤집음** | 화면이 180도 자동 회전 (가속도 센서) |

짧은 탭(LVGL `LV_EVENT_SHORT_CLICKED`, 400ms 미만 + 제자리)만 반응한다.
길게 누르기나 드래그는 무시한다. 웹 UI 의 Y축도 보드를 따라간다.

### 부팅 시 배터리 표시

부팅하면 5초간 배터리 잔량을 보여준다(`BAT_SPLASH_MS`). **화면을 터치하면
즉시 건너뛴다.** 블로킹하지 않으므로 그 사이에도 샘플링과 차트는 정상 동작한다.

```
VBAT --[ R21 200K ]--+--[ R22 100K ]-- GND
                     |
                  BAT_ADC (GPIO0)      => VBAT = 읽은 전압 x 3
```

잔량 %는 1셀 LiPo 방전 곡선 표를 선형 보간해서 구한다. 선형
(3.3V=0%, 4.2V=100%)으로 하면 중간 구간이 실제와 크게 어긋난다.

> **USB 가 꽂혀 있으면 충전 IC(ETA6098)가 전압을 끌어올려서 이 값은
> 잔량이 아니라 충전 전압이다.** 그래서 이때는 `CHARGING` 을 같이 표시한다.

> 배터리 ADC 읽기는 **`Pressure_Start()` 앞에서** 한다. 압력 센서도 같은
> ADC1 을 1 kHz 로 쓰기 때문에, 동시에 접근하면
> `adc_oneshot: adc oneshot read fail` 이 날 수 있다.

### 부팅 시 화면

`LCD_Init()` 은 백라이트를 **꺼둔 채로** 준비만 한다(`Backlight_Init()` → duty 0).
`setup()` 에서 `Chart_Init()` 까지 끝난 뒤 `lv_refr_now(NULL)` 로 첫 프레임을
실제로 그리고, 그 다음에 `Set_Backlight(LCD_BL_DEFAULT)` 로 켠다.

이렇게 하지 않으면 LVGL 이 처음 그릴 때까지 1~2초간 검은 화면이 그대로
보인다 (그 사이에 WiFi AP 기동 등이 들어간다).

## 화면 자동 회전 (180도)

QMI8658 가속도 센서(터치와 같은 I2C, 주소 0x6B)를 200ms마다 폴링해서
기기가 뒤집히면 화면을 180도 돌린다.

```c
#define ORIENT_ENABLE       1
#define ORIENT_AXIS         'x'     /* 'x' / 'y' / 'z' */
#define ORIENT_SIGN         (+1)    /* 방향이 반대면 -1 */
#define ORIENT_THRESHOLD    0.35f   /* g */
#define ORIENT_HOLD_MS       700    /* 이만큼 유지돼야 전환 */
#define ORIENT_ROT_NORMAL      3
#define ORIENT_ROT_FLIPPED     1
```

**해상도가 바뀌지 않는 `1 <-> 3` 회전만 쓴다.** 그래서 LVGL 의
`hor_res`/`ver_res` 를 건드릴 필요가 없고, 패널 MADCTL 과 터치 좌표 변환만
바꾸면 된다 (`LCD_SetRotation()` + `Touch_SetRotation()`).

> 회전 후에는 화면 좌표 매핑이 통째로 달라지므로 **전체를 무효화**해야 한다
> (`lv_obj_invalidate(lv_scr_act())` + `lv_layer_top()`). 부분 갱신만
> 하면 이전 내용이 뒤집힌 채로 남는다.

> `Touch_SetRotation()` 은 벤더 드라이버의 파일 스코프 전역 `g_rotation` 을
> 직접 갱신한다. 세터가 없고, `bsp_touch_init()` 을 다시 부르면 리셋 펄스에
> 0.5초가 걸려서 실시간 전환에 못 쓴다.

### 축 맞추기

IMU 가 보드에 어떤 방향으로 붙어 있는지에 따라 `ORIENT_AXIS` / `ORIENT_SIGN`
이 달라진다. 시리얼 **`a`** 로 확인한다.

```
[IMU] ax=+0.98  ay=-0.03  az=+0.11 g   axis 'x' x 1 = +0.98   (threshold 0.35)
```

평소 방향에서 이 값이 **+**, 뒤집었을 때 **−** 가 되는 축을 고른다.
부호가 반대면 `ORIENT_SIGN` 을 `-1` 로.

## 하드웨어 개조

### R19 제거 — 전원 LED 끄기

```
VSYS ──[ R19 3K ]──▶|── GND
                   LED2
```

**LED2 는 GPIO 가 아니라 VSYS 에 직결된 전원 표시등이다.** 펌웨어로는 끌 수
없고, 슬립 중에도 계속 켜져 있다.

소비 전류는 `(VSYS - Vf) / 3k ≈ 0.6 mA`. **라이트 슬립 상태에서는 이것이
가장 큰 소비원**으로, MCU 슬립 전류보다 훨씬 크다. 그래서 R19 를 떼어낸다.

- LED2 대신 **R19(저항)를 떼는 쪽이 쉽다**
- VSYS 에서 갈라져 나온 막다른 가지라 **떼어도 다른 동작에 영향이 없다**
- 뗀 뒤에는 전원 표시등이 없어진다. 동작 확인은 화면과 시리얼로 한다

> 대기전류를 실제로 재려면 10 mA 분해능의 벤치 파워서플라이로는 안 되고
> µA 를 읽는 장비가 필요하다 (멀티미터 µA 레인지, 전류 프로파일러 등).

## 전원 관리

이 보드에는 전원을 물리적으로 끊는 회로(래칭 로드 스위치)가 없다.
그래서 "전원 끄기" 는 **라이트 슬립**이다.

| | |
|---|---|
| 끄기 | 1분 무변화 시 자동 (또는 시리얼 `o`) |
| 켜기 | 화면 터치 |

### 자동 슬립

`EQTool.ino`

```c
#define AUTO_SLEEP_MS       60000   /* 무변화 지속 시간. 0 = 기능 끔 */
#define AUTO_SLEEP_WARN_MS  10000   /* 마지막 10초는 화면에 카운트다운 */
#define AUTO_SLEEP_PA        200.0f /* 이보다 작은 변화는 "변화 없음" */
```

저역통과를 거친 압력이 `AUTO_SLEEP_PA`(0.2 kPa) 이상 움직이지 않은 채
`AUTO_SLEEP_MS` 가 지나면 잔다. 다음 중 하나라도 있으면 타이머가 리셋된다.

- 압력이 0.2 kPa 이상 변함
- 화면 터치 (탭 / 누르기 / 떼기)
- 시리얼 입력
- **웹 클라이언트 접속 중** — 누가 폰으로 보고 있으면 자지 않는다
- **USB 케이블이 꽂혀 있음** (`SLEEP_SKIP_ON_USB 1`)

### USB 가 꽂혀 있으면 자지 않는다

**라이트 슬립에 들어가면 USB CDC 가 끊어져 COM 포트가 사라진다.** 시리얼
모니터가 조용해지고 포트 목록에서 없어지는 것이 정상이다. 깨우면(화면 터치)
재부팅되면서 포트가 다시 잡힌다. 개발 중에는 이게 계속 방해가 되고,
USB 로 전원을 받는 중이라면 배터리를 아낄 이유도 없다.

판정은 **`Serial.isPlugged()`** 로 한다. 내부적으로
`usb_serial_jtag_is_connected()` 이고 호스트의 SOF 프레임을 보기 때문에
**시리얼 모니터를 열지 않아도 케이블이 꽂혀 있으면 true** 다.

> `if (Serial)` 은 모니터를 닫으면 false 가 되어 다시 자버린다. 쓰지 말 것.

| 상황 | USB 꽂힘 | 배터리만 |
|---|---|---|
| 1분 무변화 자동 슬립 | 안 함 | 잔다 |
| 시리얼 `o` | **강제로 잔다** (동작 확인용) | 잔다 |

> `AUTO_SLEEP_PA` 를 너무 작게 잡으면 노이즈만으로 계속 리셋되어 영영 자지
> 않는다. 반대로 너무 크면 느린 압력 변화를 측정하는 중에 꺼진다.

끄는 순서: 화면 마지막 프레임 그리기 → 백라이트 off → `gfx->displayOff()`
→ `WiFi.mode(WIFI_OFF)` → 손 뗄 때까지 대기 → `esp_light_sleep_start()`

깨우기: 터치 컨트롤러 INT(**GPIO21**) **LOW 레벨**.
AXS5106L 은 INT 가 평소 HIGH 이고 터치하면 LOW 로 떨어진다
(벤더 드라이버가 FALLING 엣지로 `attachInterrupt` 한다).

> **왜 딥 슬립이 아닌가** — ESP32-C6 의 딥 슬립 GPIO 웨이크업은 **GPIO0~7 만**
> 지원한다. 터치 INT 가 GPIO21 이라 딥 슬립에서는 못 깨운다.
> 대기전류를 더 줄이려면 딥 슬립 + QMI8658 wake-on-motion(GPIO5 = IMU_INT1)
> 으로 바꿀 수 있다. 그 경우 "집어 들면 켜짐" 이 된다.

깨어난 뒤에는 복구하지 않고 **`esp_restart()`** 한다. WiFi AP / 샘플러 /
차트를 부분 복구하는 것보다 재부팅이 안전하고 1초면 올라온다.

**슬립에 들어가기 전 손을 뗄 때까지 기다리는 것이 중요하다.** INT 가 LOW 인
채로 자면 즉시 깨어난다.

## 시리얼 명령 (115200)

| 키 | 동작 |
|---|---|
| `p` | 주기 로그 on/off |
| `z` | 영점 재설정 (양쪽 포트 개방 상태에서) |
| `i` | 현재 상태 1회 출력 |
| `a` | 가속도 1회 출력 (자동 회전 축 맞출 때) |
| `l` | LCD 자가진단 (회전 0/1/2/3 색칠, 6초) |
| `o` | 전원 끄기 (USB 가 꽂혀 있어도 강제) |

## 주요 설정값

`EQTool.ino`

```c
#define CHART_Y_MIN       -5000   // -5 kPa  (아래 20%)
#define CHART_Y_MAX       20000   // +20 kPa (위 80%)
#define CHART_WINDOW_MS    2000   // 가로축 2초
```

`XGZP6899A.h`

```c
#define XGZP_ADC_PIN          5
#define XGZP_VDD_MV      3300.0f
#define XGZP_FS_KPA        40.0f
#define XGZP_OVERSAMPLE       8
```

> LVGL 8 의 `lv_coord_t` 는 int16 이라 차트 값은 Pa 단위로 ±32767 이 한계다.
> Y축 라벨은 그리기 이벤트에서 kPa 로 변환해 표시한다.

## 문제 해결 기록

### 업로드가 `A serial exception error occurred: Write timeout` 으로 실패

증상: 컴파일은 되는데 `Connecting...` 에서 바로 실패. 보드를 새것으로 바꿔도,
BOOT+RESET / BOOT 누른 채 USB 삽입으로 다운로드 모드에 넣어도 동일.
포트 자체는 정상 인식됨 (`COM8 (ESP32 Family Device)`, VID `0x303A` / PID `0x1001`).

원인: **Windows 쪽 COM 포트 드라이버**. 포트를 열기는 되는데 write 가 막힌 상태였다.

해결: 장치 관리자 → 포트(COM & LPT) → 해당 COM → **디바이스 제거**
(`이 장치의 드라이버를 삭제합니다` 체크) → USB 재연결 →
Windows 가 기본 `USB Serial Device` 를 새로 설치하게 함.

> 보드나 케이블을 의심하기 전에 드라이버를 먼저 보자.
> 보드 2개에서 똑같이 실패하면 거의 항상 PC 쪽이다.

### 시리얼 로그가 전혀 안 나옴

Tools → **USB CDC On Boot** 가 `Disabled` 면 `Serial` 이 USB 가 아니라
UART0(GPIO16/17) 로 간다. 이 보드는 반드시 **`Enabled`**.

### `Sketch too big` (1310720 bytes 초과)

Tools → Partition Scheme → **`Huge APP (3MB No OTA/1MB SPIFFS)`**.
용량의 대부분은 WiFi + HTTP 서버 스택이라 LVGL 을 깎아도 효과가 없다.

### USB C-to-C 케이블로는 CDC 인식 안 됨

이 보드는 **C-to-A 케이블**을 쓸 것. C-to-C 는 다운로드 모드는 잡혀도
CDC 포트가 안 뜬다.

### 부팅 직후 `Guru Meditation Error: ... (Store access fault)` — Chart_Init 에서

원인: **LVGL 8.4.0 버그**. `lv_chart_add_series()` 가 LINE/BAR 차트에서
`ser->x_points` / `ser->x_ext_buf_assigned` 를 초기화하지 않는데
(SCATTER 일 때만 채운다), 8.4.0 의 `lv_chart_remove_series()` 는

```c
if (!series->x_ext_buf_assigned && series->x_points) lv_mem_free(series->x_points);
```

를 실행한다. 시리즈 노드는 `lv_mem_alloc` 으로 잡히므로 두 필드에 쓰레기가
남아 있고, 그 쓰레기 포인터를 free 하다가 죽는다.

LVGL **8.3.10** 의 `remove_series` 에는 이 줄이 없다. 그래서 이전 보드
(ESP32-C6-LCD-1.47, lvgl 8.3.10) 에서는 같은 코드가 멀쩡히 돌았고,
터치 보드용 데모의 **lvgl 8.4.0** 으로 바꾸면서 드러났다.

우회: `EQTool.ino` 의 `chart_fix_series_x()` — 시리즈를 만들거나 넘겨받은
직후 `x_points = NULL`, `x_ext_buf_assigned = 0` 으로 직접 비운다.

> `lv_chart_remove_series()` 를 부르는 코드가 있다면 반드시 같이 적용할 것.

### LVGL 라벨에 %f 를 쓰면 "f" 가 찍힌다

`lv_label_set_text_fmt()` / `lv_snprintf()` 는 LVGL 자체 최소 구현이라
**부동소수점 포맷을 지원하지 않는다** (`lv_conf.h` 의 `LV_SPRINTF_USE_FLOAT`
기본값 0). `"%.2f V"` 를 쓰면 값 대신 `f` 가 그대로 나온다.

정수 두 개로 쪼개서 소수점을 직접 만든다.

```c
uint32_t mvi = (uint32_t)(mv + 0.5f);
lv_label_set_text_fmt(txt, "%u.%02u V", mvi / 1000, (mvi % 1000) / 10);
```

`Serial.printf()` 는 진짜 printf 라 `%f` 가 정상 동작한다. 헷갈리기 쉽다.

### 화면이 안 나올 때 — 펌웨어인지 하드웨어인지 가르기

부팅 로그가 아래처럼 **전부** 나온다면 펌웨어는 정상이다.

```
[LCD] backlight GPIO23  pwm=on  (off until first frame)
[LCD] JD9853 ready  320x172  rot=3
read: 8161                          <- 터치 칩 I2C 응답
[Touch] AXS5106L init  ...
[LVGL] init  320x172  ...
[EQTool] Y range -> -5 ~ 20 kPa     <- Chart_Init 완료
[WEB] AP "EQTool"  ...
[EQTool] setup done  ...
```

판정 요령

- **`[WEB] AP` 가 찍혔다 = 백라이트를 켜는 코드가 실행됐다.**
  `setup()` 순서가 `Chart_Init()` → `lv_refr_now()` → `Set_Backlight()` →
  `Web_Start()` 이기 때문이다. 켜라고 했는데 안 켜지면 하드웨어다.
- **`read: 8161` 이 있다 = FPC 가 꽂혀 있고 TP_VDD 도 들어간다.**
  LCD 와 터치는 같은 17핀 FPC(J3)를 쓴다. 터치만 되고 화면이 죽었다면
  LCD 쪽 핀(2~7)이나 백라이트 핀(8 LEDK / 9 LEDA)의 접촉 불량을 의심한다.
- 시리얼 **`l`** 로 언제든 `LCD_SelfTest()` 를 돌릴 수 있다. 백라이트를 강제로
  켜고 회전 0/1/2/3 을 색으로 칠한다(6초). 같은 펌웨어로 정상 보드와
  비교하는 것이 가장 빠르다.
- 어두운 곳에서 화면 가장자리에 미세한 빛이 새는지 본다.
  샌다 = 백라이트는 살아 있고 패널 데이터 쪽 문제. 완전히 깜깜 = LEDK/LEDA 쪽.

> 실제 사례: 두 보드에 같은 바이너리를 올렸는데 한쪽만 화면이 안 나왔다.
> 로그는 두 보드가 한 글자도 다르지 않았고, 결론은 그 보드의 하드웨어 문제.
