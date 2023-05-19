#include <Arduino.h>
#include <M5StickCPlus.h>
#include <WiFi.h>
#include <esp_now.h>
#include <atomic>

TFT_eSprite Disbuff = TFT_eSprite(&M5.Lcd);

// This mac address
// E8:9F:6D:0A:2F:70
// Transmitter mac address
// 78:21:84:88:6F:F4
// 10:91:A8:40:D7:24

uint8_t broadcastAddress[] = {0x10, 0x91, 0xA8, 0x40, 0xD7, 0x24};

enum Status {
  SAFE,
  ARMING,
  ARMED
};

typedef struct status_message {
    uint8_t messageType;
    Status status;
} status_message;

typedef struct control_message {
    uint8_t messageType;
    bool arm;
    bool fire;
} control_message;

status_message statusMessage;
uint64_t millisLastPing;

control_message controlMessage;

esp_now_peer_info_t peerInfo;

std::atomic<bool> commandSuccess(false);

// Callback when data is sent
void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success"
                                                  : "Delivery Fail");
    if (status == ESP_NOW_SEND_SUCCESS) {
        commandSuccess.store(true);
    }
}

// Callback when data is received
void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
    switch (incomingData[0]) {
        case 1:
            Serial.println("Status received");
            memcpy(&statusMessage, incomingData, sizeof(statusMessage));
            millisLastPing = millis();
            break;

        default:
            Serial.println("Received unknown message type");
            break;
    }
}

void setup() {
  // Init Serial Monitor
  Serial.begin(115200);

  M5.begin();             // Init M5StickCPlus.
  M5.Lcd.setRotation(1);  // Rotate the screen.
  Disbuff.createSprite(240, 135);
  Disbuff.fillRect(0, 0, 240, 135, TFT_BLACK);
  Disbuff.setTextSize(2);
  Disbuff.setCursor(10, 10);
  Disbuff.setTextColor(RED, BLACK);
  Disbuff.printf("- Osprey Defender -");
  Disbuff.pushSprite(0, 0);

  // Set device as a Wi-Fi Station
  WiFi.enableLongRange(true);
  WiFi.mode(WIFI_STA);

  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(OnDataSent);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
  }
  // Register for a callback function that will be called when data is
  // received
  esp_now_register_recv_cb(OnDataRecv);
}

uint8_t loopCntr = 0;
bool armSent = false;
bool fireSent = false;

void loop() {
  // Only update the display once a second
  if (loopCntr >= 10) {
    Disbuff.fillRect(0, 30, 240, 135, BLACK);  // clear the screen.
    Disbuff.setTextColor(WHITE, BLACK);
    Disbuff.setTextSize(3);
    Disbuff.setCursor(0, 40);
    uint64_t secondsSinceLastPing = (millis() - millisLastPing)/1000;
    Disbuff.print(" Ping: ");
    if (secondsSinceLastPing > 3) {
        Disbuff.setTextColor(RED, BLACK);
    } else if (secondsSinceLastPing > 1) {
        Disbuff.setTextColor(YELLOW, BLACK);
    } else {
        Disbuff.setTextColor(GREEN, BLACK);
    }
    Disbuff.printf("%ds", secondsSinceLastPing);
    Disbuff.setTextColor(WHITE, BLACK);
    Disbuff.setCursor(0, 80);
    if (statusMessage.status == ARMING) {
        Disbuff.setTextColor(YELLOW, BLACK);
        Disbuff.print("    ARMING   ");
    } else if (statusMessage.status == ARMED) {
        Disbuff.setTextColor(RED, BLACK);
        Disbuff.print("     ARMED   ");
    } else {
        Disbuff.setTextColor(GREEN, BLACK);
        Disbuff.print("     SAFE    ");
    }
    Disbuff.pushSprite(0, 0);
    loopCntr = 0;
  }

  M5.update();
  
  if (M5.BtnB.pressedFor(1000) && !armSent) {
    Serial.println("Button B was pressed for 1 second, arming");
    controlMessage.arm = true;
    controlMessage.fire = false;
    controlMessage.messageType = 2;
    
    Disbuff.fillRect(0, 30, 240, 135, BLACK);  // clear the screen.
    Disbuff.setTextColor(RED, BLACK);
    Disbuff.setTextSize(5);
    Disbuff.setCursor(10, 50);
    Disbuff.printf("ARMING");
    Disbuff.pushSprite(0, 0);
    
    uint8_t retries = 8;
    while (!commandSuccess.load() && retries > 0) {
        esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&controlMessage, sizeof(controlMessage));
        Serial.println(result);
        delay(100);
        retries--;
    }

    if (retries <= 0) {
        Disbuff.fillRect(0, 30, 240, 135, BLACK);  // clear the screen.
        Disbuff.setTextColor(RED, BLACK);
        Disbuff.setTextSize(4);
        Disbuff.setCursor(20, 50);
        Disbuff.printf("ARMING\n FAILED");
        Disbuff.pushSprite(0, 0);
    } 

    // Reset stuff
    loopCntr = 0;
    retries = 0;
    commandSuccess.store(false);

    // Don't reenter this block until the button is released
    armSent = true;
  }
  if (M5.BtnB.isReleased()) {
    armSent = false;
  }

  if (M5.BtnA.pressedFor(500) && !fireSent) {
    Serial.println("Button A was pressed for 500ms, firing");
    controlMessage.arm = false;
    controlMessage.fire = true;
    controlMessage.messageType = 2;
    
    Disbuff.fillRect(0, 30, 240, 135, BLACK);  // clear the screen.
    Disbuff.setTextColor(RED, BLACK);
    Disbuff.setTextSize(5);
    Disbuff.setCursor(10, 50);
    Disbuff.printf("FIRE");
    Disbuff.pushSprite(0, 0);

    uint8_t retries = 8;
    while (!commandSuccess.load() && retries > 0) {
        esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&controlMessage, sizeof(controlMessage));
        Serial.println(result);
        delay(100);
        retries--;
    }

    if (retries <= 0) {
        Disbuff.fillRect(0, 30, 240, 135, BLACK);  // clear the screen.
        Disbuff.setTextColor(RED, BLACK);
        Disbuff.setTextSize(4);
        Disbuff.setCursor(50, 50);
        Disbuff.printf("SEND\n FAILED");
        Disbuff.pushSprite(0, 0);
    }
    
    // Reset stuff
    loopCntr = 0;
    retries = 0;
    commandSuccess.store(false);

    // Don't reenter this block until the button is released
    fireSent = true;
  }
  if (M5.BtnA.isReleased()) {
    fireSent = false;
  }
  

  loopCntr++;
  delay(100);
}
