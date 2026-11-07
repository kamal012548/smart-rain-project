#define BLYNK_TEMPLATE_ID "TMPL6pyUP15ls"
#define BLYNK_TEMPLATE_NAME "Smart Rain Project"
#define BLYNK_AUTH_TOKEN "yCR0Gw5EdgJDtk-W8AXOAU0_PblqGf9s"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
// --- กำหนดขา Pin ของ ESP32 ---
#define DHTPIN 4          // ขา Data ของ DHT11 ต่อที่ GPIO 4
#define DHTTYPE DHT11     // ชนิดเซ็นเซอร์ DHT11
#define LDR_PIN 34        // ขา Analog อ่านค่าแสง LDR ต่อที่ GPIO 34
#define SERVO_PIN 13      // ขาควบคุม Micro Servo SG90 ต่อที่ GPIO 13
#define BUZZER_PIN 23     // ขา Active Buzzer ต่อที่ GPIO 23
#define LED_AUTO_PIN 19   // LED แสดงสถานะโหมด Auto (เขียว)
#define LED_ALERT_PIN 18  // LED แสดงสถานะเตือนภัย/ฝนตก (แดง)
// --- การตั้งค่าหน้าจอ OLED (128x64) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// --- กำหนดค่า Wi-Fi และ Blynk Token ---
char auth[] = "yCR0Gw5EdgJDtk-W8AXOAU0_PblqGf9s"; 
char ssid[] = "KAMAL";
char pass[] = "11111111";

DHT dht(DHTPIN, DHTTYPE);
Servo laundryServo;

// ตัวแปรเก็บสถานะระบบ
bool isAutoMode = true;     // ค่าเริ่มต้นทำงานแบบอัตโนมัติ
int servoPosition = 90;     // ค่าเริ่มต้นปกติ = 90 องศา
bool hasAlerted = false;    // ตัวแปรเช็กว่าแจ้งเตือนเสียง/Noti ไปหรือยัง (ป้องกันดังซ้ำ)
unsigned long lastBlynkSend = 0;

void setup() {
  Serial.begin(115200);

  // เริ่มต้นใช้งาน Pin ต่างๆ
  pinMode(LDR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_AUTO_PIN, OUTPUT);
  pinMode(LED_ALERT_PIN, OUTPUT);
  
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_AUTO_PIN, LOW);   
  digitalWrite(LED_ALERT_PIN, LOW);

  // เริ่มต้นใช้งาน Servo
  laundryServo.attach(SERVO_PIN);
  laundryServo.write(90); // เริ่มต้นที่ตำแหน่ง 90 องศา (สภาวะปกติ)

  // เริ่มต้นใช้งาน DHT และ OLED
  dht.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,10);
  display.println("Connecting to WiFi...");
  display.display();

  // เชื่อมต่อ Wi-Fi และ Blynk
  Blynk.begin(auth, ssid, pass);
  
  display.clearDisplay();
}

// รับค่าคำสั่งจาก Blynk App (Virtual Pin V3 สำหรับปุ่มเปิด/ปิดโหมด Auto)
BLYNK_WRITE(V3) {
  int pinValue = param.asInt();
  isAutoMode = (pinValue == 1);
}

// รับค่าคำสั่งจาก Blynk App (Virtual Pin V4 สำหรับกดสั่งเก็บ/กางผ้าแบบ Manual)
BLYNK_WRITE(V4) {
  int manualCommand = param.asInt(); 
  if (!isAutoMode) {
    if (manualCommand == 1) {
      laundryServo.write(0);   
      servoPosition = 0;
    } else {
      laundryServo.write(90);  
      servoPosition = 90;
    }
  }
}

void loop() {
  Blynk.run();

  // อ่านค่าจากเซ็นเซอร์
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int ldrValue = analogRead(LDR_PIN); // อ่านค่าดิบ 0 - 4095

  // แปลงค่า LDR เป็นเปอร์เซ็นต์ (0% ถึง 100%)
  int lightPercent = map(ldrValue, 0, 4095, 100, 0);
  lightPercent = constrain(lightPercent, 0, 100); 

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  // --- จัดการสถานะไฟ LED สีเขียวตามโหมดปัจจุบัน ---
  if (isAutoMode) {
    digitalWrite(LED_AUTO_PIN, HIGH); // ถ้าอยู่โหมด Auto เปิดไฟเขียวติดค้าง

    // --- เงื่อนไขการทำงานอัตโนมัติ (Auto Mode) ---
    if (humidity > 85.0 || ldrValue > 500) { 
      servoPosition = 0;                // สั่ง Servo หมุนไปที่ 0 องศา (เก็บผ้า)
      laundryServo.write(servoPosition);
      digitalWrite(LED_ALERT_PIN, HIGH);  // เปิด LED สีแดง

      // ถ้ายังไม่เคยเตือนในเหตุการณ์ฝนตกรอบนี้ ให้ส่งเสียงและแจ้งเตือน
      if (!hasAlerted) {
        // --- สั่งให้ Buzzer ดัง 3 ครั้ง แล้วหยุดทันที ---
        for (int i = 0; i < 3; i++) {
          digitalWrite(BUZZER_PIN, HIGH);   
          delay(300);                       // ดัง 0.3 วินาที
          digitalWrite(BUZZER_PIN, LOW);
          delay(200);                       // เว้นช่วง 0.2 วินาที
        }
        
        // ส่ง Push Notification เตือนเข้าแอป Blynk (ส่งครั้งเดียวจบ)
        Blynk.logEvent("rain_warning", "⚠️ ฟ้ามืดและความชื้นสูง ฝนกำลังจะตก ราวผ้าถูกเก็บอัตโนมัติแล้ว!");
        
        hasAlerted = true; // ล็อกสถานะไว้ว่าเตือนแล้ว จะไม่ดังซ้ำจนกว่าฝนจะหยุด
      }

    } else {
      // สภาวะปกติ แดดออก
      servoPosition = 90;               // สั่ง Servo กลับมาที่ 90 องศา
      laundryServo.write(servoPosition);
      digitalWrite(LED_ALERT_PIN, LOW);  // ปิด LED สีแดง
      digitalWrite(BUZZER_PIN, LOW);     // ปิด Buzzer
      
      hasAlerted = false; // รีเซ็ตสถานะการแจ้งเตือน เพื่อให้พร้อมเตือนใหม่เมื่อฝนตกรอบหน้า
    }

  } else {
    digitalWrite(LED_AUTO_PIN, LOW);   
    digitalWrite(LED_ALERT_PIN, LOW);  
    digitalWrite(BUZZER_PIN, LOW);
    hasAlerted = false;
  }

  // --- แสดงผลบนหน้าจอ OLED ---
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Mode: "); display.println(isAutoMode ? "AUTO" : "MANUAL");
  display.setCursor(0, 12);
  display.print("Temp: "); display.print(temperature); display.println(" C");
  display.setCursor(0, 24);
  display.print("Humi: "); display.print(humidity); display.println(" %");
  display.setCursor(0, 36);
  display.print("Light: "); display.print(lightPercent); display.println(" %"); 
  display.setCursor(0, 48);
  display.print("Status: "); 
  display.println(servoPosition == 0 ? "RAIN/COLLECTED" : "SUNNY/DRYING");
  display.display();

  // ส่งข้อมูลเซ็นเซอร์ขึ้น Blynk App 
  Blynk.virtualWrite(V0, temperature);
  Blynk.virtualWrite(V1, humidity);
  Blynk.virtualWrite(V2, lightPercent); 

  delay(200);
}