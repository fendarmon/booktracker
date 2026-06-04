#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>

#define buzzer_pin 25
#define led_pin 26

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

RTC_DS3231 rtc;
Preferences prefs;

struct Book {
  byte uid[4];
  const char* name;
  uint8_t id;
  bool taken;  // Has this book been taken today?
};

Book books[] = {
  {{0x04, 0xF0, 0x5F, 0x2B}, "History Book", 0, false},
  {{0xE9, 0xDE, 0x15, 0xC9}, "Physics Book", 1, false}
};

const int bookCount = sizeof(books) / sizeof(books[0]);

// Array of week days updated to 7 to prevent out-of-bounds errors
// Days: Sun=0, Mon=1, Tues=2, Wed=3, Thu=4, Fri=5, Sat=6
uint8_t weeklySchedule[7] = {0};
const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

int lastResetDay = -1; // Tracks which calendar day we last did a reset
bool allDone = false;


void saveSchedule() {
  prefs.begin("schedule", false);
  prefs.putBytes("week", weeklySchedule, 7); // Updated to 7
  prefs.end();
  Serial.println("Schedule saved.");
}

void loadSchedule(){
  prefs.begin("schedule", true);
  if (prefs.isKey("week")) {
    prefs.getBytes("week", weeklySchedule, 7); // Updated to 7
    Serial.println("Schedule loaded from storage.");
  }
  else {
    Serial.println("No saved schedule found - starting blank.");
  }
  prefs.end();
}

void printScheduleMenu () {
  Serial.println("\n╔══════════════════════════════╗");
  Serial.println("║    WEEKLY SCHEDULE EDITOR    ║");
  Serial.println("╚══════════════════════════════╝");

  for (int d = 0; d < 7; d++) { // Updated to 7
    Serial.print(d);
    Serial.print(") ");
    Serial.print(dayNames[d]);
    Serial.print(": ");
    if (weeklySchedule[d] == 0) {
      Serial.println("(no books)");
    }
    else {
      for (int b = 0; b < bookCount; b++) {
        if (weeklySchedule[d] & (1 << books[b].id)) {
          Serial.print("[");
          Serial.print(books[b].name);
          Serial.print("] ");
        }
      }
      Serial.println();
    }
  }
  Serial.println("\nEnter day number (0-6) to edit, or 'x' to exit: ");
}

void editDay(int day) {
  Serial.print("\nEditing ");
  Serial.println(dayNames[day]);

  for (int b = 0; b < bookCount; b++) {
    bool needed = weeklySchedule[day] & (1 << books[b].id);
    Serial.print(b);
    Serial.print(") ");
    Serial.print(books[b].name);
    Serial.print(" - currently ");
    Serial.println(needed ? "Needed" : "Not needed");
  }

  Serial.println("\nType book number to toggle, or 'd' when done:");

  while (true) {
    if (!Serial.available()) continue;
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "d" || input == "D") break;

    int idx = input.toInt();
    if(idx >= 0 && idx < bookCount) {
      weeklySchedule[day] ^= (1 << books[idx].id); // toggle bit
      bool nowNeeded = weeklySchedule[day] & (1 << books[idx].id);
      Serial.print("  → ");
      Serial.print(books[idx].name);
      Serial.println(nowNeeded ? " Marked as Needed" : " Marked as not needed");
    }
    else {
      Serial.println("  Invalid number.");
    }
  }
  saveSchedule();
}

void runScheduleEditor() {
  while (true) {
    printScheduleMenu();
    while (!Serial.available());
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "x" || input == "X") {
      Serial.println("Exiting Schedule editor.\n");
      return;
    }

    // Make sure input is actually a number before converting
    if (input.length() > 0 && isDigit(input[0])) {
      int day = input.toInt();
      if (day >= 0 && day < 7) {
        editDay(day);
      }
   }
    else {
      Serial.println("Invalid input.");
   }
  }
}

// Returns how many books are required today
int getTodayRequiredCount(int dayOfTheWeek) {
  int count = 0;
  for (int b = 0; b < bookCount; b++) {
    if (weeklySchedule[dayOfTheWeek] & (1 << books[b].id)) count++;
  }
  return count;
}

bool bookNeeded(int bookIndex, int dayOfTheWeek) {
  return weeklySchedule[dayOfTheWeek] & (1 << books[bookIndex].id);
}

void resetForTmrw() {
  for (int i = 0; i < bookCount; i++) books[i].taken = false;
  Serial.println("\n══════ New day — tracking reset ══════\n");
}

void printDailyProgress(int dayOfTheWeek) {
  int needed = 0, taken = 0;

  Serial.println("\n─── Today's Books ─────────────────");
  Serial.print("Day: "); Serial.println(dayNames[dayOfTheWeek]);

  for (int i = 0; i < bookCount; i++) {
    if (!bookNeeded(i, dayOfTheWeek)) continue;
    needed++;
    Serial.print(" ");
    Serial.print(books[i].name);
    Serial.print(": ");
    Serial.println(books[i].taken ? "Taken" : "Not taken");
    if (books[i].taken) taken++;
  }

  if (needed == 0) {
    Serial.println("No books needed today.");
  }
  else {
    Serial.print("\nProgress: ");
    Serial.print(taken); Serial.print(" / "); Serial.println(needed);

    if (taken == needed && !allDone) {
      allDone = true;
      Serial.println("\n🎉 All books packed — have a great day!");
      tone(buzzer_pin, 1000);
      delay(50);
      noTone(buzzer_pin);
      delay(50);
      tone(buzzer_pin, 1000);
      delay(50);
      noTone(buzzer_pin);
    }
  }
  Serial.println("────────────────────────────────────\n");
}

bool checkUID(const byte *a, const byte *b) {
  for(byte i = 0; i < 4; i++) {
    if(a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

void wrongBookAlert() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(led_pin, HIGH);
    tone(buzzer_pin, 400);   // 400Hz = low, "wrong" sounding tone
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
      Serial.print("ℹ ");
      Serial.print(books[i].name);
      Serial.println(" is not needed today.");
      wrongBookAlert();
      return;
    }
      
    if(!books[i].taken) {
      // First time taking this book
      books[i].taken = true;
      Serial.print("✓ ");
      Serial.print(books[i].name);
      Serial.println(" taken!");
      printDailyProgress(dayOfTheWeek); // This handles setting allDone to true
    }
    else {
      // Book already scanned
      Serial.print("⚠ ");
      Serial.print(books[i].name);
      Serial.println(" already taken.");
    } 
    return;
  }
  // Unknown card
  Serial.println("⚠ Unknown book detected!");
  wrongBookAlert();
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(buzzer_pin, OUTPUT);
  pinMode(led_pin, OUTPUT);

  //RTC init
  Wire.begin(21, 22);
  if(!rtc.begin()) {
    Serial.println("RTC not found, check wiring");
    while (true);
  }  

  
  // Only set the time if the RTC lost power (battery removed/dead)
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time to compile time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }


  //RFID init
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);

  // load saved schedule
  loadSchedule();

  Serial.println(F("=== Smart Textbook Tracker ==="));
  Serial.println(F("Scan books as you take them!\n"));
  Serial.println(F("Type 'S' in Serial Monitor to open the schedule monitor.\n"));
  
  // We initialize the print, which also assesses if allDone should be true today
  printDailyProgress(rtc.now().dayOfTheWeek());
}

void loop() {
  mfrc522.PCD_Init();
  DateTime now = rtc.now();
  int today = now.dayOfTheWeek();

  // First run initialization
  if (lastResetDay == -1) {
    lastResetDay = now.day();
  }
  // Day change logic - ONLY triggers if previous day's books are completely packed
  else if (now.day() != lastResetDay) {
    if (allDone) { 
      lastResetDay = now.day();
      resetForTmrw();
      allDone = false; // Reset the flag for the new day
      printDailyProgress(today);
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'S' || c == 's') {
      runScheduleEditor();
      printDailyProgress(today);  // refresh after editing
    }
  }
  

  // Wait for a card to be scanned
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Card detected!");
    handleBookScan(mfrc522.uid.uidByte, today);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(1000);  // Prevent multiple rapid reads of the same card
  }
}