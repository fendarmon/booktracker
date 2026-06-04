#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>


const char* ssid = "Schedule Tracker";
const char* password = "123456789";

#define buzzer_pin 25
#define led_pin 26

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

RTC_DS3231 rtc;
Preferences prefs;
WebServer server(80); // Web server on port 80

struct Book {
  byte uid[4];
  const char* name;
  uint8_t id;
  bool taken;  
};

Book books[] = {
  {{0x04, 0xF0, 0x5F, 0x2B}, "History Book", 0, false},
  {{0xE9, 0xDE, 0x15, 0xC9}, "Physics Book", 1, false}
};

const int bookCount = sizeof(books) / sizeof(books[0]);

uint8_t weeklySchedule[7] = {0};
const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

int lastResetDay = -1; 
bool allDone = false;

// --- Schedule Storage ---
void saveSchedule() {
  prefs.begin("schedule", false);
  prefs.putBytes("week", weeklySchedule, 7); 
  prefs.end();
  Serial.println("Schedule saved.");
}

void loadSchedule(){
  prefs.begin("schedule", true);
  if (prefs.isKey("week")) {
    prefs.getBytes("week", weeklySchedule, 7); 
    Serial.println("Schedule loaded from storage.");
  } else {
    Serial.println("No saved schedule found - starting blank.");
  }
  prefs.end();
}

bool bookNeeded(int bookIndex, int dayOfTheWeek) {
  return weeklySchedule[dayOfTheWeek] & (1 << books[bookIndex].id);
}

void resetForTmrw() {
  for (int i = 0; i < bookCount; i++) books[i].taken = false;
  Serial.println("\n══════ New day — tracking reset ══════\n");
}

void checkAllDoneLog(int dayOfTheWeek) {
  int needed = 0, taken = 0;
  for (int i = 0; i < bookCount; i++) {
    if (bookNeeded(i, dayOfTheWeek)) {
      needed++;
      if (books[i].taken) taken++;
    }
  }
  if (needed > 0 && taken == needed && !allDone) {
    allDone = true;
    Serial.println("\n🎉 All books packed — have a great day!");
    tone(buzzer_pin, 1000); delay(50); noTone(buzzer_pin); delay(50);
    tone(buzzer_pin, 1000); delay(50); noTone(buzzer_pin);
  }
}

// --- Web Server Request Handlers ---

// Serves the main HTML Dashboard
void handleRoot() {
  int today = rtc.now().dayOfTheWeek();
  
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family:Arial,sans-serif; background:#f4f4f9; color:#333; padding:20px;}";
  html += ".card{background:white; padding:20px; border-radius:10px; margin-bottom:20px; box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
  html += "h1, h2{color:#4A90E2;} .btn{background:#4A90E2; color:white; border:none; padding:8px 12px; border-radius:5px; cursor:pointer; text-decoration:none; display:inline-block;}";
  html += ".btn-toggle{background:#5cbae6; font-size:0.9em; margin:2px;} .needed{color:green; font-weight:bold;} .not-needed{color:#999;}";
  html += "</style><title>Smart Textbook Tracker</title></head><body>";
  
  html += "<h1>📚 Smart Textbook Tracker</h1>";
  
  // Section 1: Today's Status
  html += "<div class='card'><h2>Today's Status (" + String(dayNames[today]) + ")</h2><ul>";
  int neededCount = 0;
  for (int i = 0; i < bookCount; i++) {
    if (bookNeeded(i, today)) {
      neededCount++;
      html += "<li><strong>" + String(books[i].name) + ":</strong> " + (books[i].taken ? "✅ Packed" : "❌ Missing") + "</li>";
    }
  }
  if (neededCount == 0) html += "<li>No books needed today! 😊</li>";
  html += "</ul></div>";

  // Section 2: Interactive Weekly Schedule Editor
  html += "<div class='card'><h2>Weekly Schedule Editor</h2>";
  for (int d = 0; d < 7; d++) {
    html += "<h3>" + String(dayNames[d]) + (d == today ? " (Today)" : "") + "</h3>";
    html += "<ul>";
    for (int b = 0; b < bookCount; b++) {
      bool isNeeded = bookNeeded(b, d);
      html += "<li>" + String(books[b].name) + " - ";
      if (isNeeded) {
        html += "<span class='needed'>Needed</span> ";
      } else {
        html += "<span class='not-needed'>Not Needed</span> ";
      }
      // Link triggers the toggle action via URL parameters
      html += "<a class='btn btn-toggle' href='/toggle?day=" + String(d) + "&book=" + String(b) + "'>Toggle</a>";
      html += "</li>";
    }
    html += "</ul>";
  }
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// Processes the toggle inputs from the buttons on the dashboard
void handleToggle() {
  if (server.hasArg("day") && server.hasArg("book")) {
    int day = server.arg("day").toInt();
    int bookIdx = server.arg("book").toInt();

    if (day >= 0 && day < 7 && bookIdx >= 0 && bookIdx < bookCount) {
      weeklySchedule[day] ^= (1 << books[bookIdx].id); // toggle bit mask
      saveSchedule();
    }
  }
  // Redirect back to the home dashboard instantly
  server.sendHeader("Location", "/");
  server.send(303);
}

void wrongBookAlert() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(led_pin, HIGH);
    tone(buzzer_pin, 400);   
    delay(150);
    digitalWrite(led_pin, LOW);
    noTone(buzzer_pin);
    delay(100);
  }
}

void handleBookScan(const byte *scannedUID, int dayOfTheWeek) {
  for(int i = 0; i < bookCount; i++) {
    if(!checkUID(scannedUID, books[i].uid)) continue;

    if (!bookNeeded(i, dayOfTheWeek)) {
      Serial.println("ℹ Book not needed today.");
      wrongBookAlert();
      return;
    }
      
    if(!books[i].taken) {
      books[i].taken = true;
      Serial.println("✓ Book taken!");
      checkAllDoneLog(dayOfTheWeek); 
    } else {
      Serial.println("⚠ Already taken.");
    } 
    return;
  }
  Serial.println("⚠ Unknown book detected!");
  wrongBookAlert();
}

bool checkUID(const byte *a, const byte *b) {
  for(byte i = 0; i < 4; i++) {
    if(a[i] != b[i]) return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(buzzer_pin, OUTPUT);
  pinMode(led_pin, OUTPUT);

  // RTC Init
  Wire.begin(21, 22);
  if(!rtc.begin()) {
    Serial.println("RTC not found!");
    while (true);
  }  
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // RFID Init
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  loadSchedule();

  // --- Connect to Wi-Fi ---
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP()); // Look closely at this in the Serial Monitor!

  // --- Configure Server Routing ---
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.begin();
  Serial.println("HTTP Server started.");
}

void loop() {
  // Let the server listen for incoming client requests asynchronously
  server.handleClient();

  mfrc522.PCD_Init();
  DateTime now = rtc.now();
  int today = now.dayOfTheWeek();

  if (lastResetDay == -1) {
    lastResetDay = now.day();
  }
  else if (now.day() != lastResetDay) {
    if (allDone) { 
      lastResetDay = now.day();
      resetForTmrw();
      allDone = false; 
    }
  }

  // Check for physical RFID cards
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Card detected!");
    handleBookScan(mfrc522.uid.uidByte, today);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(1000); 
  }
}