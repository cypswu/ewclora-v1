/*
   LORA函式庫使用https://github.com/sandeepmistry/arduino-LoRa
   SSD1306 單色 128x64 and 128x32 OLEDs：https://github.com/adafruit/Adafruit_SSD1306
   LoRa 雙向通訊，使用中斷呼叫接收測試，用 ESP8285 連接 Ra-02 加 OLED 顯示
   ESP8285接腳定義：
     LoRa SPI : NSS<->IO16, NRESET<->IO2, DIO0<->IO15, MOSI<->IO13, MISO<->IO12, SCK<->IO14, VCC:3.3
     OLED I2C : SDA<->IO4, SCL<->IO5, VCC:3.3
     LED : IO0
   ESP8266開發版本：2.5.0
   硬體開發板：EWCLORA-V1
*/
#include <SPI.h>              // include libraries
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const long Frequency = 433E6;        // LoRa Frequency in Hz (433E6, 866E6, 915E6)
const int SpreadingFactor = 7;       // LoRa Spreading Factor, default 7, between 6 and 12
const long SignalBandwidth = 125E3;  // LoRa SignalBand, default 125E3, values are 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, and 250E3
const int CodingRate4 = 5;           // LoRa Coding Rate Denominator, default 5, values are between 5 and 8
const int TxPower = 17;              // LoRa TX power in dB, defaults to 17, values are 2 to 20
const byte CS_PIN = 16;              // LoRa radio chip select default 10
const byte RESET_PIN = 2;            // LoRa radio reset default 9
const byte IRQ_PIN = 15;             // change for your board; must be a hardware interrupt pin default 2
const byte LED = 0;                  // 燈號指示

unsigned int msgCount = 0;           // count of outgoing messages
byte localAddress;                   // address of this device (本設備)
unsigned long lastSendTime = 0;      // last send time
int  interval = 2000;                // interval between sends

volatile int larssi = 0;
volatile float lasnr = 0;
volatile boolean is_rec = false;
String recMesg;
String sendMesg;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  // OLED初始化
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) // initialize with the I2C addr 0x3C (for the 128x64)
    Serial.println(F("SSD1306 allocation failed"));
  // 設備編號初始化
  randomSeed(analogRead(A0));
  localAddress = random(255);  // 初始化設備編號
  Serial.println("LoRa Duplex with callback, I am 0x" + String(localAddress, HEX));

  // override the default CS, reset, and IRQ pins (optional)
  LoRa.setPins(CS_PIN, RESET_PIN, IRQ_PIN); // set CS, reset, IRQ pin

  LoRa.setSPIFrequency(8E6); // SPI 8M Hz, default 10M Hz
  if (!LoRa.begin(Frequency)) {             // initialize ratio at 433 MHz
    Serial.println("LoRa init failed.");
    showOLEDText("LoRa init failed.");
    while (true);                       // if failed, do nothing
  }

  LoRa.setSpreadingFactor(SpreadingFactor);
  LoRa.setSignalBandwidth(SignalBandwidth);
  LoRa.setCodingRate4(CodingRate4);
  LoRa.setTxPower(TxPower);
  LoRa.enableCrc();

  LoRa.onReceive(onReceive);
  LoRa.receive();
  Serial.println("LoRa 0x" + String(localAddress, HEX) + " init succeeded.");
}

void loop() {
  unsigned long mdif;
  mdif = getMillisDif(lastSendTime, millis());
  yield();
  if (mdif > interval) {
    lastSendTime = millis();            // timestamp the message
    interval = random(2000) + 3000;     // 3-5 seconds
    sendMessage();
    showOLED();
    Serial.println("Send");
  }
  // 顯示OLED訊息(OLED放在onReceive中無法display)
  if (is_rec) {
    showOLED();
    showSerial();
    showLed();  // 接收燈號強弱
    is_rec = false;
  }
}
/*
   計算時間間距
*/
unsigned long getMillisDif(unsigned long time1, unsigned long time2) {
  unsigned long stmpTime;
  if (time1 > time2)
    stmpTime = 4294967295UL - time1 + time2 + 1UL;
  else
    stmpTime = time2 - time1;
  return stmpTime;
}
/*
   LoRa訊號發送
*/
void sendMessage() {
  msgCount++;                         // increment message ID
  sendMesg = "EWC 0x";
  sendMesg += String(localAddress, HEX);
  sendMesg += " ";
  sendMesg += msgCount;   // send a message
  sendMesg += " ";
  sendMesg += interval;   // next send time
  sendMesg += "ms";
  yield();
  LoRa.beginPacket();                   // start packet
  LoRa.print(sendMesg);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  Serial.print("Sent: ");
  Serial.println(sendMesg);
  LoRa.receive();
}
/*
   LoRa訊號接收
*/
void onReceive(int packetSize) {
  if (packetSize == 0) {
    interrupts();
    return;          // if there's no packet, return
  }
  Serial.println("Receive");
  String incoming = "";                  // payload of packet
  for (int i = 0; i < packetSize; i++) {
    incoming += (char)LoRa.read();
  }
  recMesg = incoming;
  larssi = LoRa.packetRssi();
  lasnr = LoRa.packetSnr();
  is_rec = true;
}

/*
    接收 RSSI 轉換為燈號強弱 從 RSSI -20~-140
*/
void showLed() {
  static boolean ledOn = LOW;
  int val;
  if (larssi > -20)
    val = -20;
  else if (larssi < -140)
    val = -140;
  else
    val = larssi;
  val = map(val, -20, -140, 255, 20);
  if (ledOn == HIGH) {
    analogWrite(LED, 0);
    ledOn = LOW;
  }  else {
    analogWrite(LED, val);
    ledOn = HIGH;
  }
}
/*
   顯示接收訊息與訊號強度
*/
void showSerial() {
  Serial.print("Received: ");
  Serial.print(recMesg.c_str());
  Serial.print(" ,RSSI: ");
  Serial.print(larssi);
  Serial.print(" ,Snr: ");
  Serial.print(lasnr);
  Serial.println();
}

/*
   顯示OLED訊息
*/
void showOLED() {
  Serial.println("Show OLED");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Tx: ");
  display.println(sendMesg);
  display.print("Rx: ");
  display.println(recMesg);
  display.setTextSize(2);
  display.print("RSSI=");
  display.println(String(larssi));
  display.print("SNR=");
  display.println(lasnr);
  display.display();
  delay(1);
}
/*
   顯示指定字串到OLED
*/
void showOLEDText(String txt) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(txt);
  display.display();
  delay(1);
}
