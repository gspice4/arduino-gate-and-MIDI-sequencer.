// Enigma_Inc
// Riley Clarke

// Bugs include 74HC595 not working. Otherwise, horray!

// 1.24.26
// Mega 2560 Pro
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <SD.h>
#include <SPI.h>

// --------------------------
// SCREEN MANAGEMENT
// --------------------------

enum Screen {
  MAIN,
  SEQUENCER,
  STEP,
  GATE,
  SETTINGS,
  TEMPO,
  FIRMWARE,
  OUTMAX,
  CONFIG,
  FILE_MENU,
  INFO,
  SAVE,
  SAVE_AS,
  LOAD,
  SAVE_CONFIRM_SCREEN
};

const int SCREEN_STACK_SIZE = 5;
Screen screenStack[SCREEN_STACK_SIZE];
int stackTop = 0;

void pushScreen(Screen s) {
  if (stackTop < SCREEN_STACK_SIZE) {
    screenStack[stackTop++] = s;
  }
}

void popScreen() {
  if (stackTop > 0) stackTop--;
}

Screen currentScreen() {
  if (stackTop == 0) return MAIN;
  return screenStack[stackTop - 1];
}

// ---------------- EEPROM ----------------
const int numSlots = 16;
const int eepromStart = 0;
const int eepromTempoAddr = eepromStart + numSlots;
const int eepromVelocityStart = eepromTempoAddr + 1;

// ---------------- DISPLAY ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- SD -----------------
#define SD_CS 53
// ---------------- FILE / SD MENU VARIABLES ----------------
#define MAX_FILES 20 
const int maxFiles = 16;          // max files displayed
String fileList[MAX_FILES];       // Stores filenames from SD
int fileCount = 0;               // How many .ktw files were found
int fileCursor = 0;              // Which file is highlighted
int fileScrollIndex = 0;         // Scrolls the file list like main menu

// ---------------- 74HC595 STUFF -----------------
const int dataPin = 11;
const int clockPin = 13;
const int latchPin = 10;

uint16_t outputData = 0;
uint16_t lastOutputData = 0;
// ---------------- INCRAMENT -----------------
bool sliderBigStep = false;         // false = small step, true = big step

// ---------------- BUTTON PINS ----------------
const int pinBack = 5;
const int pinDown = 4;
const int pinUp = 3;
const int pinEnter = 2;

// const int pinNC = 22;
const int pinPlay = 23;
const int pinStop = 24;

const int pinIncrementToggle = 38;
const int pinReset = 36;
const int pinSave = 34;
// const int pinSaveAs = 32;
// const int pinload = 30;

// ---------------- DEBOUNCE ----------------
const int debounceDelay = 150;
unsigned long lastPressTime[8] = {0};
bool wasPressed[8] = {false};

// ---------------- BUTTON EDGE ----------------
bool readButtonEdge(int pin, int index) {
  bool reading = digitalRead(pin) == LOW;
  if (reading && !wasPressed[index] && (millis() - lastPressTime[index] > debounceDelay)) {
    wasPressed[index] = true;
    lastPressTime[index] = millis();
    return true;
  }
  if (!reading) wasPressed[index] = false;
  return false;
}



// ---------------- MENU VARIABLES ----------------
const char* menuItems[] = {"NOTE SLCT", "SETTINGS", "FILE"}; 
const int totalMenuItems = sizeof(menuItems) / sizeof(menuItems[0]);
int mainCursorSelect = 0;
const int menuDisplayLimit = 2; // limit of options per screen in MAIN MENU
int menuScrollIndex = 0;

int settingsCursor = 0;  // Cursor in settings
int sliderValue = 120;  // Default tempo
const int sliderMin = 40;  // min BPM
const int sliderMax = 240;  // max BPM
const int numSettingsOptions = 4; // # of options in settings menu
const char* settingsOptions[] = { "Tempo", "Firmware", "Outs", "Config" }; // options in settings menu

const int settingsDisplayLimit = 2;     // how many fit on screen
int settingsScrollIndex = 0;            // top visible item


// ---------------- NOTE SELECT ----------------
int midiNotes[numSlots];
int noteCursor = 0;
int currentSlot = 0;
int midiVelocity[numSlots];

int outMax = 4;
int gateOutput[numSlots];

// ---------------- SEQUENCER ----------------
bool sequencerRunning = false;
int currentStep = 0;
int lastStep = -1;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 150;
int timeDiv = 4;
int curSeq;
int seqMax;
// ---------------- SEQUENCER CONFIG. ----------------
int configSlct = 0;
int configNumItems = 7;
const char* configItems[] = {"Single", "Pair", "Triple", "Full", "Double", "Stack 4", "Stack 3", "Stack 2"};
// ---------------- EEPROM HELPERS ----------------
void saveSequenceToEEPROM() {
  for(int i=0;i<numSlots;i++) EEPROM.update(eepromStart+i,gateOutput[i]);
}

void loadSequenceFromEEPROM() {
  for(int i=0;i<numSlots;i++) {
    int val = EEPROM.read(eepromStart+i);
    gateOutput[i] = (val>=0 && val<=127) ? val : 1;
  }
}

void saveTempoToEEPROM() { EEPROM.update(eepromTempoAddr, sliderValue); }

void loadTempoFromEEPROM() { 
  int val = EEPROM.read(eepromTempoAddr);
  sliderValue = (val>=sliderMin && val<=sliderMax) ? val : 120;
}

void saveAllToEEPROM() {
  saveSequenceToEEPROM();
  saveTempoToEEPROM();
  pushScreen(SAVE_CONFIRM_SCREEN);
}

// ---------------- SEQUENCER ----------------
void updateStepInterval() {
  stepInterval = 60000UL / (sliderValue * timeDiv);
}

void updateSequencer() {
  if(!sequencerRunning) return;
  unsigned long now = millis();
  if(now-lastStepTime >= stepInterval) {
    lastStepTime = now;
    if(lastStep >= 0) setBit(gateOutput[currentStep], false);
    setBit(gateOutput[currentStep], true);
    lastStep = currentStep;
    currentStep = (currentStep+1)%numSlots;
    Serial.println(currentStep);
  }
}
// ---------------- CLEAR HELPER ----------------
void resetEEPROM() {
  // Reset notes
  for (int i = 0; i < numSlots; i++) {
    gateOutput[i] = 1; // default gate output
  }

  // Reset tempo
  sliderValue = 120;

  // Save new values
  saveSequenceToEEPROM();
  saveTempoToEEPROM();

  // Update timings
  updateStepInterval();
}

// ---------------- DISPLAY FUNCTIONS ----------------

void displayScrollMenu(
  const char* title,
  const char* items[],
  int totalItems,
  int displayLimit,
  int cursor,
  int scrollIndex,
  bool showBackArrow,
  int textPadding
  ) 
  {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title
  if (title != nullptr) {
    display.setCursor(34, 2);
    display.print(title);
  }

  // Back arrow
  if (showBackArrow) {
    display.setCursor(0, 1);
    display.print("<-");
  }

  int startY = title ? 22 : 12;
  int itemHeight = 18;

  for (int i = 0; i < displayLimit; i++) {
    int itemIndex = scrollIndex + i;
    if (itemIndex >= totalItems) break;

    int y = startY + i * itemHeight;

    if (itemIndex == cursor) {
      display.fillRect(20, y, 86, 16, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(34, y + textPadding);
    display.print(items[itemIndex]);
  }

  // Scroll indicators
  display.setTextColor(SSD1306_WHITE);
  if (scrollIndex > 0) {
    display.setCursor(110, 0);
    display.print("^");
  }
  if (scrollIndex + displayLimit < totalItems) {
    display.setCursor(110, SCREEN_HEIGHT - 8);
    display.print("v");
  }

  display.display();
}

// -------------- MENUS ---------------

void displayMainMenu() {
  displayScrollMenu(
    "MAIN MENU",
    menuItems,
    totalMenuItems,
    menuDisplayLimit,
    mainCursorSelect,
    menuScrollIndex,
    false,
    4
  );
}

void settingsCursorDown() {
  if (settingsCursor < numSettingsOptions - 1) {
    settingsCursor++;

    if (settingsCursor >= settingsScrollIndex + settingsDisplayLimit) {
      settingsScrollIndex = settingsCursor - settingsDisplayLimit + 1;
    }
  }
}

void settingsCursorUp() {
  if (settingsCursor > 0) {
    settingsCursor--;

    if (settingsCursor < settingsScrollIndex) {
      settingsScrollIndex = settingsCursor;
    }
  }
}

void displaySettingsMenu() {
  display.setTextColor(SSD1306_WHITE);
  displayScrollMenu(
    "SETTINGS",                 // no title
    settingsOptions,
    numSettingsOptions,    // 4 items
    settingsDisplayLimit,    // e.g. 3 visible
    settingsCursor,
    settingsScrollIndex,
    true,                     // show back arrow
    4
  );
}


// -------------- FILE MENU ---------------
void displayFileMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,1); display.print("<-");
  display.setCursor(47, 2); display.print("FILE");

  const char* items[] = {"Info", "Save As", "Load"};

  for (int i = 0; i < 3; i++) {
    int y = 18 + i * 16;
    if (i == fileCursor) {
      display.fillRect(20, y, 88, 14, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(25, y + 3);
    display.print(items[i]);
  }

  display.display();
}

// ------------- SAVE AS ---------------
void displaySave() {

}

// ------------- INFO ---------------
void displayInfo() {

}

// ------------- TEMPO SLIDER ---------------
void displaySliderScreen() {
  display.clearDisplay();
  display.setCursor(0,1); display.print("<-");
  display.setCursor(49,5); display.print("Tempo:");
  display.setTextSize(2);
  display.setCursor(sliderValue<100?55:48,20);
  display.print(sliderValue);
  display.setTextSize(1);
  int barX=20,barY=50,barWidth=86,barHeight=8;
  display.drawRect(barX,barY,barWidth,barHeight,SSD1306_WHITE);
  display.fillRect(barX,barY,map(sliderValue,sliderMin,sliderMax,0,barWidth),barHeight,SSD1306_WHITE);

    if (sliderBigStep) {
    display.setCursor(120,0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.print("I");
  }

  display.display();
}

// ---------------- FIRMWARE ----------------
void displayFirmwareScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,1); display.print("<-");
  display.setCursor(39,5); display.print("Firmware");
  display.setTextSize(2); display.setCursor(18,30); display.print("7205-8-B");
  display.setTextSize(1); display.setCursor(44,55); display.print("(Beta)"); // Stable = 39 | Beta = 44
  display.display();
}

// ---------------- ACTIVATE OUTS SCREEN -----------------
void displayOutMax() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,1); display.print("<-"); // BACK ARROW
  display.setCursor(25,0); display.print("Activate Outs");
  display.setTextSize(2); int om = outMax; display.setCursor(om<10?58:51,26); display.print(om);
  display.setTextSize(1);

  // LEFT ARROW
  display.fillTriangle(21, 30, 21, 30, 18, 33, SSD1306_WHITE);
  display.fillTriangle(18, 33, 21, 36, 21, 30, SSD1306_WHITE);

  // RIGHT ARROW
  display.fillTriangle(106, 30, 106, 30, 109, 33, SSD1306_WHITE);
  display.fillTriangle(109, 33, 106, 36, 106, 30, SSD1306_WHITE);

  if (sliderBigStep) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(120,0);
    display.print("I");
  }
  display.display();
}

void displayConfig() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,1); display.print("<-");
  display.setTextSize(1); display.setCursor(40,30); display.print(configItems[configSlct]);

  switch (configSlct) {
    case 0: // Single
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      seqMax = 0;
      break;

    case 1: // Pair
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(115, 5, 4, 3, SSD1306_WHITE);
      seqMax = 1;
      break;

    case 2: // Triple
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(115, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(110, 5, 4, 3, SSD1306_WHITE);
      seqMax = 2;
      break;

    case 3: // Full
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(115, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(110, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(105, 5, 4, 3, SSD1306_WHITE);
      seqMax = 3;
      break;

    case 4: // Double
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(115, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(115, 9, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 9, 4, 3, SSD1306_WHITE);
      seqMax = 3;
      break;
    
    case 5: // Stack 4
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 9, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 13, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 17, 4, 3, SSD1306_WHITE);
      seqMax = 3;
      break;

    case 6: // Stack 3
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 9, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 13, 4, 3, SSD1306_WHITE);
      seqMax = 2;
      break;

    case 7: // Stack 2
      display.drawRect(120, 5, 4, 3, SSD1306_WHITE);
      display.drawRect(120, 9, 4, 3, SSD1306_WHITE);
      seqMax = 1;
      break;

  }
   // LEFT ARROW
  display.fillTriangle(21, 30, 21, 30, 18, 33, SSD1306_WHITE);
  display.fillTriangle(18, 33, 21, 36, 21, 30, SSD1306_WHITE);

  // RIGHT ARROW
  display.fillTriangle(106, 30, 106, 30, 109, 33, SSD1306_WHITE);
  display.fillTriangle(109, 33, 106, 36, 106, 30, SSD1306_WHITE);

  display.display();
}

void displaySeqPick() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,1); display.print("<-");
  display.setTextSize(1); display.setCursor(33,30); display.print("Sequencer "); display.print(curSeq+1);

   // LEFT ARROW
  display.fillTriangle(21, 30, 21, 30, 18, 33, SSD1306_WHITE);
  display.fillTriangle(18, 33, 21, 36, 21, 30, SSD1306_WHITE);

  // RIGHT ARROW
  display.fillTriangle(106, 30, 106, 30, 109, 33, SSD1306_WHITE);
  display.fillTriangle(109, 33, 106, 36, 106, 30, SSD1306_WHITE);

  display.display();
}

// ------------ NOTE SELECT MENU ----------------
void displayNoteSelectMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,1); display.print("<-");
  display.setCursor(33,1); display.print("Sequencer "); display.print(curSeq+1);

  const int cols=4, cellW=26, cellH=12, startX=9, startY=10;
  for(int i=0;i<numSlots;i++){
    int col=i%cols,row=i/cols,x=startX+col*(cellW+2),y=startY+row*(cellH+2);
    if(i==noteCursor){
      display.fillRect(x,y,cellW,cellH,SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else if(sequencerRunning && i==currentStep){
      display.drawRect(x-1,y-1,cellW+2,cellH+2,SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.drawRect(x,y,cellW,cellH,SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(x+6,y+2);
    display.print(i+1);
  }
  display.display();
}

// ---------------- GATE SCREEN -----------------
void displayGate() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,1); display.print("<-"); // BACK ARROW
  display.setCursor(44,5); display.print("Step "); display.print(currentSlot+1);  // STEP #
  display.setTextSize(2); int go = gateOutput[currentSlot]; display.setCursor(go<100?55:48,26); display.print(go);

  // LEFT ARROW
  display.fillTriangle(21, 30, 21, 30, 18, 33, SSD1306_WHITE);
  display.fillTriangle(18, 33, 21, 36, 21, 30, SSD1306_WHITE);

  // RIGHT ARROW
  display.fillTriangle(106, 30, 106, 30, 109, 33, SSD1306_WHITE);
  display.fillTriangle(109, 33, 106, 36, 106, 30, SSD1306_WHITE);

  if (sliderBigStep) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(118, 1, 9, 10, SSD1306_WHITE);
    display.setCursor(120,0);
    display.print("I");
  }
  display.display();
}

// SAVE CONFIRM
void displaySaveConfirmScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(25,25); display.print("Saved");
  display.display();
}

// EEPROM CLEAR
void displayEEPROMClearScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(25,25); display.print("Cleared");
  display.display();
}

void runSplashScreens() {
  display.clearDisplay();
  display.setTextSize(2); display.setTextColor(SSD1306_WHITE); display.setCursor(21,20); display.print(F("_ENIGMA")); display.display();
  delay(1500);
  display.setTextSize(1); display.setCursor(52,48); display.print(F("K-12")); display.display();
  delay(2500);
  display.clearDisplay(); display.setTextSize(1); display.setCursor(9,11); display.print(F("Developed By"));
  display.setTextSize(2); display.setCursor(10,34); display.print(F("R. CLARKE")); display.display();
  delay(500);
  display.clearDisplay();
}

// ---------------- SETUP ----------------
void setup() {
  display.begin(SSD1306_SWITCHCAPVCC,0x3C);

  pinMode(pinBack,INPUT_PULLUP); 
  pinMode(pinDown,INPUT_PULLUP); 
  pinMode(pinUp,INPUT_PULLUP); 
  pinMode(pinEnter,INPUT_PULLUP);
  pinMode(pinPlay,INPUT_PULLUP); 
  pinMode(pinStop,INPUT_PULLUP);
  pinMode(pinIncrementToggle, INPUT_PULLUP);
  pinMode(pinReset, INPUT_PULLUP);

  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin, OUTPUT);

  Serial.begin(115200);
  pinMode(10, OUTPUT);

  Serial.println("Initializing SD card...");

  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed!");
  } else {
    Serial.println("SD OK!");
  }

  for (int i = 0; i < numSlots; i++) {
    Serial.print(gateOutput[i]);
  }
  for(int i=0;i<numSlots;i++) { midiNotes[i]=60; gateOutput[i]=100; }


  loadSequenceFromEEPROM();
  loadTempoFromEEPROM();

  updateStepInterval();
  runSplashScreens();
  pushScreen(MAIN);
}

// ---------------- SETBIT ----------------
void setBit(int bitNumber, bool state) {
  if (state) outputData |= (1 << bitNumber);
  else outputData &= ~(1 << bitNumber);
}
// ---------------- LOOP ----------------
void loop() {
  // Read buttons using the new enum
  bool up = readButtonEdge(pinUp, 2);
  bool down = readButtonEdge(pinDown, 1);
  bool enter = readButtonEdge(pinEnter, 3);
  bool back = readButtonEdge(pinBack, 0);
  bool playPressed = readButtonEdge(pinPlay, 4);
  bool stopPressed = readButtonEdge(pinStop, 5);
  bool incrementPressed = readButtonEdge(pinIncrementToggle, 6);
  bool resetPressed = readButtonEdge(pinReset, 7);

  // Handle reset
  if (resetPressed) {
    resetEEPROM();
    for (int i = 0; i < numSlots; i++) {
      gateOutput[i] = 0;
    }    
    pushScreen(SAVE_CONFIRM_SCREEN);
  }

  Screen scr = currentScreen();

  // Toggle step size if increment button pressed
  if (incrementPressed) sliderBigStep = !sliderBigStep;

  int step = 1; // default small step
  if (sliderBigStep) {
      if (scr == OUTMAX) step = 10;
      else if (scr == TEMPO) step = 10;
      else if (scr == GATE) step = 10;
  }

  // Sequencer control
  if(playPressed && sequencerRunning == false) sequencerRunning = true;
  if(stopPressed && sequencerRunning == true) {
    sequencerRunning = false;
    if(lastStep >= 0) setBit(gateOutput[currentStep], false);
    setBit(gateOutput[currentStep], true);
    currentStep = 0;
    lastStep = -1;
  }

  updateSequencer();

  if (Serial.available() > 0) {
    String incomingMessage = Serial.readString();
    Serial.println("Okey!");
    for (int i = 0; i < numSlots; i++) {
      Serial.print(gateOutput[i]);
    }    
  }
      
    

  if (outputData != lastOutputData) {
    digitalWrite(latchPin, LOW);

    shiftOut(dataPin, clockPin, MSBFIRST, highByte(outputData));
    shiftOut(dataPin, clockPin, MSBFIRST, lowByte(outputData));

    digitalWrite(latchPin, HIGH);

    lastOutputData = outputData;
  }

  // ---------------- SCREEN LOGIC ----------------
  switch(scr) {
    case MAIN:
      if(up && mainCursorSelect > 0){ mainCursorSelect--; if(mainCursorSelect < menuScrollIndex) menuScrollIndex--; }
      if(down && mainCursorSelect < totalMenuItems - 1){ mainCursorSelect++; if(mainCursorSelect >= menuScrollIndex + menuDisplayLimit) menuScrollIndex++; }
      if(enter){
        if(mainCursorSelect==0) pushScreen(SEQUENCER);
        else if(mainCursorSelect==1) pushScreen(SETTINGS);
        else if(mainCursorSelect==2) pushScreen(FILE_MENU);
      }
      displayMainMenu();
      break;

    case SEQUENCER:
      if(down && curSeq > 0) curSeq--;
      if(up && curSeq < seqMax - 1) curSeq++;
      if(enter) {
        if(curSeq==0) pushScreen(STEP);
        if(curSeq==1) pushScreen(STEP);
        if(curSeq==2) pushScreen(STEP);
        if(curSeq==3) pushScreen(STEP);
      }
      if(back) popScreen();
      displaySeqPick();
      break;

    case STEP:
      if(down && noteCursor > 0) noteCursor--;
      if(up && noteCursor < numSlots - 1) noteCursor++;
      if(enter){ currentSlot = noteCursor; pushScreen(GATE); }
      if(back) popScreen();
      displayNoteSelectMenu();
      break;

    case GATE:
      if(up && gateOutput[currentSlot] < outMax) gateOutput[currentSlot] = min(gateOutput[currentSlot]+step,outMax);
      if(down && gateOutput[currentSlot] > 1) gateOutput[currentSlot] = max(gateOutput[currentSlot]-step,1);
      if(back) popScreen();
      if(enter) popScreen();
      displayGate();
      break;

    case SETTINGS:
      if(up && settingsCursor > 0) {settingsCursorUp();}
      if(down && settingsCursor < numSettingsOptions - 1) {settingsCursorDown();}
      if(enter && settingsCursor == 0) pushScreen(TEMPO);
      if(enter && settingsCursor == 1) pushScreen(FIRMWARE);
      if(enter && settingsCursor == 2) pushScreen(OUTMAX);
      if(enter && settingsCursor == 3) pushScreen(CONFIG);
      if(back) popScreen();
      displaySettingsMenu();
      break;

    case TEMPO:
      if(up && sliderValue < sliderMax) { sliderValue = min(sliderValue+step, sliderMax); updateStepInterval(); }
      if(down && sliderValue > sliderMin) { sliderValue = max(sliderValue-step, sliderMin); updateStepInterval(); }
      if(back) popScreen();
      if(enter) popScreen();
      displaySliderScreen();
      break;

    case FIRMWARE:
      if(back) popScreen();
      displayFirmwareScreen();
      break;

    case OUTMAX:
      if(up && outMax < 16) outMax++;
      if(down && outMax > 1) outMax--;
      if(enter) popScreen();
      if(back) popScreen();
      displayOutMax();
      break;

    case CONFIG:
      if(up && configSlct < configNumItems) configSlct++;
      if(down && configSlct > 0) configSlct--;
      if(enter) popScreen();
      if(back) popScreen();
      displayConfig();
      break;
    case SAVE_CONFIRM_SCREEN:
      displayEEPROMClearScreen();
      delay(800);
      popScreen();
      break;

    case FILE_MENU:
      if (up && fileCursor > 0) fileCursor--;
      if (down && fileCursor < 2) fileCursor++; // Save, Save As, Load

      if (enter) {
        if (fileCursor == 0) pushScreen(INFO);
        else if (fileCursor == 1) pushScreen(SAVE_AS);
        else if (fileCursor == 2) {
            pushScreen(LOAD);
        }
      }
      if (back) popScreen();
      displayFileMenu(); // You need to make this
      break;


    case SAVE:
      if (enter) {
          saveAllToEEPROM();
          popScreen(); 
          popScreen();
      }
      if (back) popScreen();
      displaySave();  // no arguments
      break;


    case SAVE_AS:
      if (back) popScreen();
      if (enter) popScreen();
      break;

    case LOAD:
      // Navigate the file list
      if (up && fileCursor > 0) fileCursor--;
      if (down && fileCursor < fileCount - 1) fileCursor++;
      if (back) popScreen();
      break;


    }
  }