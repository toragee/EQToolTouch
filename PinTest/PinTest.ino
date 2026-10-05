/*
 * PinTest - GPIO4 가 "떠 있는지" 아니면 "센서가 구동하는지" 판별
 *
 *  배선: 센서 VDD -> 3V3 / 센서 GND -> GND / 센서 OUT -> GPIO4 (H1 17번)
 *
 *  Tools 설정: USB CDC On Boot = Enabled
 *
 *  판정표
 *  ------------------------------------------------------------------
 *  ADC ~1600~1700 mV                 -> 정상. 센서가 VDD/2 를 구동 중
 *  pulldown=0, pullup=1              -> 핀이 떠 있음 (아무것도 구동 안 함)
 *                                       = OUT 단선 / 센서 GND 미연결 / 센서 사망
 *  pulldown=1, pullup=1, ADC~3100 mV -> 무언가 HIGH 로 강하게 구동 중
 *                                       = 출력 레일 포화 or 디지털(SDA) 핀
 *  ------------------------------------------------------------------
 */

#define OUT_PIN 4

void setup()
{
    Serial.begin(115200);
    delay(2000);
    analogReadResolution(12);
    Serial.println();
    Serial.println("=== GPIO4 drive test ===");
    Serial.println("ADC / pulldown / pullup");
}

void loop()
{
    /* 1) 아날로그 모드: 내부 풀 저항이 해제된 상태의 실제 전압 */
    pinMode(OUT_PIN, ANALOG);
    delay(10);
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(OUT_PIN);
    uint32_t mv = sum / 16;

    /* 2) 내부 풀다운(약 45K)을 걸어서 끌어내려지는지 본다 */
    pinMode(OUT_PIN, INPUT_PULLDOWN);
    delay(10);
    int d_dn = digitalRead(OUT_PIN);

    /* 3) 내부 풀업을 걸어본다 */
    pinMode(OUT_PIN, INPUT_PULLUP);
    delay(10);
    int d_up = digitalRead(OUT_PIN);

    Serial.printf("ADC=%4u mV   pulldown=%d   pullup=%d", mv, d_dn, d_up);

    if (mv > 1400 && mv < 1900)
        Serial.println("   -> OK: 센서가 VDD/2 구동 중");
    else if (d_dn == 0 && d_up == 1)
        Serial.println("   -> 핀이 떠 있음 (OUT 단선 / 센서 GND / 센서 불량)");
    else if (d_dn == 1 && d_up == 1)
        Serial.println("   -> HIGH 로 강하게 구동됨 (레일 포화 or 디지털 핀)");
    else
        Serial.println("   -> 판정 애매");

    delay(1000);
}
