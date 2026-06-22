// =====================================================
// GainesLabs R&E - FULL BUILD
// =====================================================

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <Preferences.h>

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================================================
// Pins
// =====================================================

#define IR_RECV_PIN 4
#define IR_SEND_PIN 5

#define BTN_UP     1
#define BTN_DOWN   2
#define BTN_SELECT 3

#define CC1101_GDO0 6
#define CC1101_GDO2 7

// =====================================================
// Give me your info lmao (jk)
// =====================================================

const char* ssid = "WifiNameHere";
const char* password = "WifiPasswrd";

// =====================================================
// Display
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =====================================================
// Update Server
// =====================================================

WebServer server(80);

// =====================================================
// IR
// =====================================================

IRrecv irrecv(IR_RECV_PIN);
decode_results results;
IRsend irsend(IR_SEND_PIN);

// =====================================================
// SubGhz - Not implimented 
// =====================================================

RCSwitch radio = RCSwitch();

// =====================================================
// Storage
// =====================================================

Preferences prefs;

// =====================================================
// Limits
// =====================================================

#define MAX_IR_SIGNALS 20
#define MAX_RF_SIGNALS 20

// =====================================================
// Input lockout - fix for mistake 2x inputs
// =====================================================

unsigned long inputLockUntil = 0;

bool inputsLocked() {
  return millis() < inputLockUntil;
}

void lockInputs(int ms = 150) {
  inputLockUntil = millis() + ms;
}

void waitForSelectRelease() {

  while (!digitalRead(BTN_SELECT)) {
    delay(10);
  }

  delay(40);
}

// =====================================================
// UI pt 2
// =====================================================

enum ScreenState {
  SCREEN_HOME,
  SCREEN_IR_LIST,
  SCREEN_IR_SLOT,
  SCREEN_RF_LIST,
  SCREEN_RF_SLOT
};

ScreenState currentScreen = SCREEN_HOME;

// =====================================================
// Menu curses
// =====================================================

int homeCursor = 0;

int irCursor = 0;
int irSlotCursor = 0;

int rfCursor = 0;
int rfSlotCursor = 0;

// =====================================================
// IR Storage
// =====================================================

String irNames[MAX_IR_SIGNALS];
uint32_t irValues[MAX_IR_SIGNALS];
uint8_t irBits[MAX_IR_SIGNALS];

// =====================================================
// RF Storage - Future Use
// =====================================================

String rfNames[MAX_RF_SIGNALS];
unsigned long rfValues[MAX_RF_SIGNALS];
unsigned int rfBits[MAX_RF_SIGNALS];
unsigned int rfProtocols[MAX_RF_SIGNALS];

// =====================================================
// Menus
// =====================================================

const char* irSlotMenu[] = {
  "Transmit",
  "Receive",
  "Rename",
  "Reset",
  "< Back"
};

const char* rfSlotMenu[] = {
  "Transmit",
  "Receive",
  "Rename",
  "Reset",
  "< Back"
};

// =====================================================
// OTA Page
// =====================================================

const char* uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<body style='font-family:sans-serif;background:#111;color:white;'>
<h2>FORD R&E OTA</h2>

<form method='POST' action='/update' enctype='multipart/form-data'>
<input type='file' name='update'>
<input type='submit' value='Upload Firmware'>
</form>

<br>
<a href='/manage'>Manage Slots</a>

</body>
</html>
)rawliteral";

// =====================================================
// Tx Animation
// =====================================================

void txAnimation(const char* label) {

  for (int i = 0; i < 3; i++) {

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(28,18);
    display.print(label);

    display.drawCircle(64,42,6 + (i * 6), SSD1306_WHITE);

    display.display();

    delay(90);
  }
}

// =====================================================
// Storage
// =====================================================

void loadStorage() {

  prefs.begin("storage", true);

  for (int i = 0; i < MAX_IR_SIGNALS; i++) {

    irNames[i] = prefs.getString(("irn" + String(i)).c_str(), "IR Slot " + String(i));

    irValues[i] = prefs.getUInt(("irv" + String(i)).c_str(), 0);

    irBits[i] = prefs.getUChar(("irb" + String(i)).c_str(), 32);
  }

  for (int i = 0; i < MAX_RF_SIGNALS; i++) {

    rfNames[i] = prefs.getString(("rfn" + String(i)).c_str(), "RF Slot " + String(i));

    rfValues[i] = prefs.getULong(("rfv" + String(i)).c_str(), 0);

    rfBits[i] = prefs.getUInt(("rfb" + String(i)).c_str(), 24);

    rfProtocols[i] = prefs.getUInt(("rfp" + String(i)).c_str(), 1);
  }

  prefs.end();
}

void saveIR(int slot) {

  prefs.begin("storage", false);

  prefs.putString(("irn" + String(slot)).c_str(), irNames[slot]);
  prefs.putUInt(("irv" + String(slot)).c_str(), irValues[slot]);
  prefs.putUChar(("irb" + String(slot)).c_str(), irBits[slot]);

  prefs.end();
}

void saveRF(int slot) {

  prefs.begin("storage", false);

  prefs.putString(("rfn" + String(slot)).c_str(), rfNames[slot]);
  prefs.putULong(("rfv" + String(slot)).c_str(), rfValues[slot]);
  prefs.putUInt(("rfb" + String(slot)).c_str(), rfBits[slot]);
  prefs.putUInt(("rfp" + String(slot)).c_str(), rfProtocols[slot]);

  prefs.end();
}

// =====================================================
// Icon UI
// =====================================================

void drawWifiIcon(int x, int y, bool selected) {

  // WiFi arcs
  display.drawCircle(x, y, 2, SSD1306_WHITE);

  display.drawCircle(x, y, 8, SSD1306_WHITE);
  display.fillRect(x - 8, y, 16, 8, SSD1306_BLACK);

  display.drawCircle(x, y, 14, SSD1306_WHITE);
  display.fillRect(x - 14, y, 28, 14, SSD1306_BLACK);

  // label
  display.setCursor(x - 5, y + 18);
  display.print("IR");

  // underline if selected
  if (selected) {
    display.drawLine(x - 14, y + 28, x + 14, y + 28, SSD1306_WHITE);
  }
}

void drawRadioIcon(int x, int y, bool selected) {

  // tower
  display.drawLine(x, y - 12, x - 6, y + 8, SSD1306_WHITE);
  display.drawLine(x, y - 12, x + 6, y + 8, SSD1306_WHITE);
  display.drawLine(x - 4, y + 2, x + 4, y + 2, SSD1306_WHITE);

  // signal waves
  display.drawCircle(x, y - 12, 10, SSD1306_WHITE);
  display.fillRect(x - 10, y - 12, 20, 12, SSD1306_BLACK);

  display.drawCircle(x, y - 12, 16, SSD1306_WHITE);
  display.fillRect(x - 16, y - 12, 32, 16, SSD1306_BLACK);

  // label
  display.setCursor(x - 21, y + 18);
  display.print("Sub-Ghz");

  // underline if selected
  if (selected) {
    display.drawLine(x - 22, y + 28, x + 22, y + 28, SSD1306_WHITE);
  }
}

void drawHome() {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setCursor(32, 2);
  display.print("FORD R&E");

  // IR icon
  drawWifiIcon(36, 34, homeCursor == 0);

  // RF icon
  drawRadioIcon(92, 34, homeCursor == 1);

  display.display();
}

// =====================================================
// IR List 
// =====================================================

void drawIRList() {

  display.clearDisplay();

  for (int i = 0; i < 5; i++) {

    int idx = i + (irCursor / 5) * 5;

    if (idx >= MAX_IR_SIGNALS + 2) break;

    display.setCursor(0, i * 12);

    display.print(idx == irCursor ? "> " : "  ");

    if (idx < MAX_IR_SIGNALS)
      display.print(irNames[idx]);

    else if (idx == MAX_IR_SIGNALS)
      display.print("TV-BEE-GONE");

    else
      display.print("< BACK");
  }

  display.display();
}

// =====================================================
// RF list - not finsihed here either
// =====================================================

void drawRFList() {

  display.clearDisplay();

  for (int i = 0; i < 5; i++) {

    int idx = i + (rfCursor / 5) * 5;

    if (idx >= MAX_RF_SIGNALS + 1) break;

    display.setCursor(0, i * 12);

    display.print(idx == rfCursor ? "> " : "  ");

    if (idx < MAX_RF_SIGNALS)
      display.print(rfNames[idx]);

    else
      display.print("< BACK");
  }

  display.display();
}

// =====================================================
// Draw Slots for IR
// =====================================================

void drawIRSlot() {

  display.clearDisplay();

  display.setCursor(0,0);
  display.print(irNames[irCursor]);

  for (int i = 0; i < 5; i++) {

    display.setCursor(0,14 + (i * 10));

    display.print(i == irSlotCursor ? "> " : "  ");
    display.print(irSlotMenu[i]);
  }

  display.display();
}

// =====================================================
// Draw slots for RF
// =====================================================

void drawRFSlot() {

  display.clearDisplay();

  display.setCursor(0,0);
  display.print(rfNames[rfCursor]);

  for (int i = 0; i < 5; i++) {

    display.setCursor(0,14 + (i * 10));

    display.print(i == rfSlotCursor ? "> " : "  ");
    display.print(rfSlotMenu[i]);
  }

  display.display();
}

// =====================================================
// IR
// =====================================================

void sendIRSignal(int slot) {

  if (irValues[slot] == 0) return;

  for (int i = 0; i < 3; i++) {

    txAnimation("Sending IR");

    irsend.sendNEC(irValues[slot], irBits[slot]);

    delay(120);
  }

  drawIRSlot();
}

void learnIRSignal(int slot) {

  display.clearDisplay();
  display.setCursor(18,20);
  display.print("Waiting IR...");
  display.display();

  unsigned long start = millis();

  while (millis() - start < 15000) {

    ArduinoOTA.handle();
    server.handleClient();

    if (irrecv.decode(&results)) {

      irValues[slot] = results.value;
      irBits[slot] = results.bits;

      saveIR(slot);

      irrecv.resume();

      display.clearDisplay();
      display.setCursor(30,28);
      display.print("IR SAVED");
      display.display();

      delay(900);

      drawIRSlot();

      return;
    }
  }

  display.clearDisplay();
  display.setCursor(26,28);
  display.print("IR TIMEOUT");
  display.display();

  delay(900);

  drawIRSlot();
}

// =====================================================
// RF
// =====================================================

void sendRFSignal(int slot) {

  if (rfValues[slot] == 0) return;

  for (int i = 0; i < 3; i++) {

    txAnimation("Sending RF");

    radio.setProtocol(rfProtocols[slot]);

    radio.send(rfValues[slot], rfBits[slot]);

    delay(120);
  }

  drawRFSlot();
}

void learnRFSignal(int slot) {

  display.clearDisplay();
  display.setCursor(18,20);
  display.print("Waiting RF...");
  display.display();

  unsigned long start = millis();

  while (millis() - start < 15000) {

    ArduinoOTA.handle();
    server.handleClient();

    if (radio.available()) {

      rfValues[slot] = radio.getReceivedValue();
      rfBits[slot] = radio.getReceivedBitlength();
      rfProtocols[slot] = radio.getReceivedProtocol();

      saveRF(slot);

      radio.resetAvailable();

      display.clearDisplay();
      display.setCursor(30,28);
      display.print("RF SAVED");
      display.display();

      delay(900);

      drawRFSlot();

      return;
    }
  }

  display.clearDisplay();
  display.setCursor(26,28);
  display.print("RF TIMEOUT");
  display.display();

  delay(900);

  drawRFSlot();
}

// =====================================================
// Web Slot Manager
// =====================================================

String buildManagePage() {

  String page;

  page += "<html><body style='background:#111;color:white;font-family:sans-serif;'>";
  page += "<h2>FORD R&E SLOT MANAGER</h2>";

  page += "<form action='/saveNames'>";

  page += "<h3>IR SLOTS</h3>";

  for (int i = 0; i < MAX_IR_SIGNALS; i++) {

    page += "IR ";
    page += String(i);
    page += ": <input name='ir";
    page += String(i);
    page += "' value='";
    page += irNames[i];
    page += "'><br><br>";
  }

  page += "<h3>RF SLOTS</h3>";

  for (int i = 0; i < MAX_RF_SIGNALS; i++) {

    page += "RF ";
    page += String(i);
    page += ": <input name='rf";
    page += String(i);
    page += "' value='";
    page += rfNames[i];
    page += "'><br><br>";
  }

  page += "<input type='submit' value='SAVE'>";
  page += "</form>";

  page += "<br><a href='/'>Back</a>";

  page += "</body></html>";

  return page;
}

// =====================================================
// OTA + WEB
// =====================================================

void setupOTA() {

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {

    ArduinoOTA.setHostname("FORD-RE");
    ArduinoOTA.begin();

    server.on("/", []() {
      server.send(200, "text/html", uploadPage);
    });

    server.on("/manage", []() {
      server.send(200, "text/html", buildManagePage());
    });

    server.on("/saveNames", []() {

      for (int i = 0; i < MAX_IR_SIGNALS; i++) {

        String arg = "ir" + String(i);

        if (server.hasArg(arg)) {

          irNames[i] = server.arg(arg);

          saveIR(i);
        }
      }

      for (int i = 0; i < MAX_RF_SIGNALS; i++) {

        String arg = "rf" + String(i);

        if (server.hasArg(arg)) {

          rfNames[i] = server.arg(arg);

          saveRF(i);
        }
      }

      drawHome();

      server.send(200, "text/html",
        "<html><body style='background:#111;color:white;'>"
        "<h2>SAVED</h2>"
        "<a href='/manage'>Back</a>"
        "</body></html>");
    });

    server.on("/update", HTTP_GET, []() {
      server.send(200, "text/html", uploadPage);
    });

    server.on("/update", HTTP_POST,

      []() {
        server.send(200, "text/plain", "DONE");
        ESP.restart();
      },

      []() {

        HTTPUpload& upload = server.upload();

        if (upload.status == UPLOAD_FILE_START) {
          Update.begin(UPDATE_SIZE_UNKNOWN);
        }

        else if (upload.status == UPLOAD_FILE_WRITE) {
          Update.write(upload.buf, upload.currentSize);
        }

        else if (upload.status == UPLOAD_FILE_END) {
          Update.end(true);
        }
      }
    );

    server.begin();
  }
}

// =====================================================
// Input
// =====================================================

void handleButtons() {

  if (inputsLocked()) return;

  // HOME

  if (currentScreen == SCREEN_HOME) {

    if (!digitalRead(BTN_UP) || !digitalRead(BTN_DOWN)) {

      homeCursor = !homeCursor;

      drawHome();

      delay(150);
    }

    if (!digitalRead(BTN_SELECT)) {

      if (homeCursor == 0) {

        currentScreen = SCREEN_IR_LIST;
        drawIRList();
      }

      else {

        currentScreen = SCREEN_RF_LIST;
        drawRFList();
      }

      waitForSelectRelease();
      lockInputs();
    }
  }

  // IR LIST

  else if (currentScreen == SCREEN_IR_LIST) {

    if (!digitalRead(BTN_UP)) {

      irCursor--;

      if (irCursor < 0)
        irCursor = MAX_IR_SIGNALS + 1;

      drawIRList();

      delay(150);
    }

    if (!digitalRead(BTN_DOWN)) {

      irCursor++;

      if (irCursor > MAX_IR_SIGNALS + 1)
        irCursor = 0;

      drawIRList();

      delay(150);
    }

    if (!digitalRead(BTN_SELECT)) {

      if (irCursor == MAX_IR_SIGNALS + 1) {

        currentScreen = SCREEN_HOME;
        drawHome();
      }

      else if (irCursor == MAX_IR_SIGNALS) {

        sendIRSignal(0);
      }

      else {

        currentScreen = SCREEN_IR_SLOT;
        irSlotCursor = 0;
        drawIRSlot();
      }

      waitForSelectRelease();
      lockInputs();
    }
  }

  // RF LIST

  else if (currentScreen == SCREEN_RF_LIST) {

    if (!digitalRead(BTN_UP)) {

      rfCursor--;

      if (rfCursor < 0)
        rfCursor = MAX_RF_SIGNALS;

      drawRFList();

      delay(150);
    }

    if (!digitalRead(BTN_DOWN)) {

      rfCursor++;

      if (rfCursor > MAX_RF_SIGNALS)
        rfCursor = 0;

      drawRFList();

      delay(150);
    }

    if (!digitalRead(BTN_SELECT)) {

      if (rfCursor == MAX_RF_SIGNALS) {

        currentScreen = SCREEN_HOME;
        drawHome();
      }

      else {

        currentScreen = SCREEN_RF_SLOT;
        rfSlotCursor = 0;
        drawRFSlot();
      }

      waitForSelectRelease();
      lockInputs();
    }
  }

  // IR SLOT

  else if (currentScreen == SCREEN_IR_SLOT) {

    if (!digitalRead(BTN_UP)) {

      irSlotCursor--;

      if (irSlotCursor < 0)
        irSlotCursor = 4;

      drawIRSlot();

      delay(150);
    }

    if (!digitalRead(BTN_DOWN)) {

      irSlotCursor++;

      if (irSlotCursor > 4)
        irSlotCursor = 0;

      drawIRSlot();

      delay(150);
    }

    if (!digitalRead(BTN_SELECT)) {

      switch (irSlotCursor) {

        case 0:
          sendIRSignal(irCursor);
          break;

        case 1:
          learnIRSignal(irCursor);
          break;

        case 3:
          irValues[irCursor] = 0;
          saveIR(irCursor);
          break;

        case 4:
          currentScreen = SCREEN_IR_LIST;
          drawIRList();
          break;
      }

      waitForSelectRelease();
      lockInputs();
    }
  }

  // RF SLOT

  else if (currentScreen == SCREEN_RF_SLOT) {

    if (!digitalRead(BTN_UP)) {

      rfSlotCursor--;

      if (rfSlotCursor < 0)
        rfSlotCursor = 4;

      drawRFSlot();

      delay(150);
    }

    if (!digitalRead(BTN_DOWN)) {

      rfSlotCursor++;

      if (rfSlotCursor > 4)
        rfSlotCursor = 0;

      drawRFSlot();

      delay(150);
    }

    if (!digitalRead(BTN_SELECT)) {

      switch (rfSlotCursor) {

        case 0:
          sendRFSignal(rfCursor);
          break;

        case 1:
          learnRFSignal(rfCursor);
          break;

        case 3:
          rfValues[rfCursor] = 0;
          saveRF(rfCursor);
          break;

        case 4:
          currentScreen = SCREEN_RF_LIST;
          drawRFList();
          break;
      }

      waitForSelectRelease();
      lockInputs();
    }
  }
}

// =====================================================
// Setup
// =====================================================

void setup() {

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  Wire.begin(8,9);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(30,28);
  display.print("BOOTING...");

  display.display();

  irrecv.enableIRIn();
  irsend.begin();

  ELECHOUSE_cc1101.Init();

  radio.enableReceive(digitalPinToInterrupt(CC1101_GDO0));
  radio.enableTransmit(CC1101_GDO2);

  loadStorage();

  setupOTA();

  drawHome();
}

// =====================================================
// Loop
// =====================================================

void loop() {

  ArduinoOTA.handle();
  server.handleClient();

  handleButtons();
}