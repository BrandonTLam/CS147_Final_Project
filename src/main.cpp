#include <Arduino.h>
#include <Servo.h>
#include <WiFi.h>
#include <HttpClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// -------------------- Wi-Fi CONFIG --------------------
char ssid[] = "Brandons";
char pass[] = "Brandonlam1";
const char kServerHost[] = "18.219.39.137";
const int  kServerPort  = 5000;

// -------------------- PINS & SERVO --------------------
const int IR_PIN     = 27;
const int LED_PIN    = 21;
const int SERVO_PIN  = 18;
const int BUZZER_PIN = 26;
const bool IR_ACTIVE_LOW = true;

const int SERVO_REST_ANGLE   = 90;
const int SERVO_ACTIVE_ANGLE = 0;

Servo myservo;

// -------------------- TWINKLE TWINKLE SONG FOR BUZZER --------------------
const int NOTE_C4 = 262, NOTE_D4 = 294, NOTE_E4 = 330;
const int NOTE_F4 = 349, NOTE_G4 = 392, NOTE_A4 = 440;

const int TWINKLE_LEN = 14;

const int twinkleFreqs[TWINKLE_LEN] = {
  NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4,
  NOTE_A4, NOTE_A4, NOTE_G4, NOTE_F4,
  NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4,
  NOTE_D4, NOTE_C4
};

const unsigned long Q = 300, H = 600;
const unsigned long twinkleDurations[TWINKLE_LEN] = {
  Q, Q, Q, Q, Q, Q, H,
  Q, Q, Q, Q, Q, Q, H
};

// -------------------- LOGGING TO AWS CLOUD SERVER --------------------
struct LogEvent {
  char name[32];
};

QueueHandle_t logQueue = nullptr;

// Sends a single log event to the remote HTTP server if WiFi is connected
void logEvent(const char *event) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("[LOGGER] WiFi not connected, cannot log event: ");
    Serial.println(event);
    return;
  }

  WiFiClient c;
  HttpClient http(c);
  char path[64];
  snprintf(path, sizeof(path), "/?var=%s", event);

  Serial.print("[LOGGER] Logging event to server: ");
  Serial.println(path);

  int err = http.get(kServerHost, kServerPort, path, NULL);
  if (err == 0) {
    int status = http.responseStatusCode();

    if (status < 200 || status >= 300) {
      Serial.print("[LOGGER] Server returned ERROR status: ");
      Serial.println(status);
    }

    http.skipResponseHeaders(); 
  } else {
    Serial.print("[LOGGER] Connect failed: ");
    Serial.println(err);
  }

  http.stop();
}

// Queues a log event string for asynchronous sending by the logger task
void enqueueLog(const char *event) {
  if (!logQueue) return;
  LogEvent ev;
  strncpy(ev.name, event, sizeof(ev.name) - 1);
  ev.name[sizeof(ev.name) - 1] = '\0';
  xQueueSend(logQueue, &ev, 0); 
}

// Waits for queued log events and sends them to the server
void loggerTask(void *param) {
  LogEvent ev;
  for (;;) {
    if (xQueueReceive(logQueue, &ev, portMAX_DELAY) == pdTRUE) {
      logEvent(ev.name);
    }
  }
}

// -------------------- MELODY --------------------
// Generates a tone on the buzzer at the given frequency for the given duration
void playTone(int freq, unsigned long durMs) {
  unsigned long startMs = millis();
  if (freq <= 0) {
    while (millis() - startMs < durMs) delay(1);
    return;
  }
  unsigned long halfPeriodUs = 1000000UL / (2UL * (unsigned long)freq);
  while (millis() - startMs < durMs) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(halfPeriodUs);
  }
}

// Plays the full "Twinkle Twinkle Little Star" melody on the buzzer until completely finished
void playTwinkleBlocking() {
  for (int i = 0; i < TWINKLE_LEN; i++) {
    playTone(twinkleFreqs[i], twinkleDurations[i]);
  }
}

// -------------------- Door --------------------
// Runs one full door cycle: open door, turn on LED, play melody, then close door and turn off outputs
void runDoorCycle() {
  Serial.println("Motion detected: Door Open, Led On, and Buzzer On ");
  myservo.write(SERVO_ACTIVE_ANGLE);
  digitalWrite(LED_PIN, HIGH);

  playTwinkleBlocking();

  Serial.println("Door Closed, Led Off, Buzzer Off");
  myservo.write(SERVO_REST_ANGLE);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

// -------------------- HELPERS --------------------
// Reads the IR sensor and returns true if motion/object is detected based on active-low setting
bool irDetected() {
  int raw = digitalRead(IR_PIN);
  return IR_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

// -------------------- SETUP --------------------
// Initializes serial, WiFi, pins, servo, logging queue, logger task, and prints ready status
void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(IR_PIN, IR_ACTIVE_LOW ? INPUT_PULLUP : INPUT);

  myservo.attach(SERVO_PIN);
  myservo.write(SERVO_REST_ANGLE);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  logQueue = xQueueCreate(10, sizeof(LogEvent));
  if (logQueue) {
    xTaskCreatePinnedToCore(
      loggerTask,
      "LoggerTask",
      4096,
      nullptr,
      1,
      nullptr,
      0
    );
  } else {
    Serial.println("Failed to create log queue!");
  }

  Serial.println("System ready: Sensor + Led + Buzzer + Async Logging.");
}

// -------------------- LOOP --------------------
// Continuously monitors the IR sensor, runs the door cycle and logs events when motion is detected.
void loop() {
  if (irDetected()) {
    enqueueLog("motion_detected_and_door_open");
    runDoorCycle();
    enqueueLog("door_closed");

    // wait until object leaves
    while (irDetected()) {
      delay(50);
    }
  }
  delay(20); 
}