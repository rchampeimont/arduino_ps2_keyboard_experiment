// Copyright 2021 River Champeimont

#include <LiquidCrystal.h>

// PS/2 keyboard connections
const int PS2_KEYBOARD_CLOCK_PIN = 2;
const int PS2_KEYBOARD_DATA_PIN = 3;

// Connections to the circuit: LCD screen
const int LCD_RS_PIN = 12;
const int LCD_ENABLE_PIN = 11;
const int LCD_DATA_PIN_4 = 7;
const int LCD_DATA_PIN_5 = 8;
const int LCD_DATA_PIN_6 = 9;
const int LCD_DATA_PIN_7 = 10;

LiquidCrystal lcd(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_DATA_PIN_4, LCD_DATA_PIN_5, LCD_DATA_PIN_6, LCD_DATA_PIN_7);

const int LCD_ROWS = 2;
const int LCD_COLS = 16;

byte lastReportedKeyboardInterruptCounter = 0;

volatile byte keyboardInterruptCounter = 0;
volatile byte keyboardBitCounter = 0;
volatile byte incompleteKeycode = 0;

volatile unsigned long lastKeyboardIntrruptTime = 0;
volatile byte keyboardInterruptDeltasIndex = 0;
const byte KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE = 15;
volatile byte keyboardInterruptDeltas[KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE];


const byte KEYBOARD_BUFFER_SIZE = 8;
volatile byte keyboardBuffer[KEYBOARD_BUFFER_SIZE];
volatile byte keyboardBufferIndex = 0;
volatile byte keyboardErrorStatus = 0;

const long AUTOMATIC_RESET_AFTER = 200; // microseconds



void processKeyboardInterrupt() {
  unsigned long currentTime = micros();
  unsigned long delta = currentTime - lastKeyboardIntrruptTime;
  keyboardInterruptDeltas[keyboardInterruptDeltasIndex] = (delta > 255) ? 255 : (byte) delta;
  lastKeyboardIntrruptTime = currentTime;
  keyboardInterruptDeltasIndex++;
  if (keyboardInterruptDeltasIndex >= KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE) {
    keyboardInterruptDeltasIndex = 0;
  }

  byte data = digitalRead(PS2_KEYBOARD_DATA_PIN);

  // If a long time occured since the last 11-bit sequence, reset to reading a new value.
  // This allows us to get out of a dead end if an extraneous or missing bit was transmitted previously.
  if (delta > AUTOMATIC_RESET_AFTER) {
    incompleteKeycode = 0;
    keyboardBitCounter = 0;
    keyboardErrorStatus = 0;
  }

  if (keyboardErrorStatus) return; // stop keyboard system on error: useful for debug

  if (keyboardBitCounter == 0) {
    // start bit
    if (data != 0) {
      // invalid start bit, it should be 0
      keyboardErrorStatus = 'S';
      reportKeyboardError();
    }
  } else if (keyboardBitCounter > 0 && keyboardBitCounter < 9) {
    // data bit: least significant bit is received first.
    incompleteKeycode |= data << (keyboardBitCounter - 1);
  } else if (keyboardBitCounter == 9) {
    // we have just read parity bit, so let's check parity
    byte parity = data;
    for (byte i = 0; i < 8; i++) {
      parity += incompleteKeycode >> i & 1;
    }
    // odd parity is expected
    if (parity % 2 != 1 && !keyboardErrorStatus) {
      // incorrect parity
      keyboardErrorStatus = 'P';
      reportKeyboardError();
    }
  }

  if (keyboardBitCounter == 10) {
    // We are at stop bit.
    if (data != 1 && !keyboardErrorStatus) {
      // invalid end bit: it should be 1
      keyboardErrorStatus = 'E';
      reportKeyboardError();
    }
    // All 11 bits have been received.
    if (! keyboardErrorStatus) {
      // process received byte if every check passed
      processKeyboard11BitCode(incompleteKeycode);
    }
    // Reset to read next 11-bit chunk
    incompleteKeycode = 0;
    keyboardBitCounter = 0;
  } else {
    keyboardBitCounter++;
  }

  keyboardInterruptCounter++;

  // The delay prevents reading extra artefact bits.
  // We could do without it at the price of a few errors once in a while (like 1% of reads).
  // Tested values:
  // (my test keyboard has a clock period of around 90 usec, standard says it is between 60 and 100)
  // <=25 avoids most errors but can sometimes fail
  // 30-70 avoid all errors
  // >=80 fails because it we miss bits
  //delayMicroseconds(50);
}

const char *getKeyboardError() {
  switch (keyboardErrorStatus) {
    case 'S':
      return "START bit was 1 while 0 was expected";
    case 'E':
      return "END bit was 0 while 1 was expected";
    case 'P':
      return "PARITY was incorrect";
    case 0:
      return "no error";
    default:
      return "unknown error";
  }
}

void processKeyboard11BitCode(int keycode) {
  // Add keycode to buffer
  keyboardBuffer[keyboardBufferIndex] = keycode;
  keyboardBufferIndex++;
  if (keyboardBufferIndex >= KEYBOARD_BUFFER_SIZE) {
    keyboardBufferIndex = 0;
  }
}

void reportKeyboardError() {
  // print debug information to serial
  Serial.print("Keyboard error: ");
  Serial.write(getKeyboardError());
  Serial.println("");
  Serial.println("Latest keyboard interrupt timing deltas in microseconds (most recent first):");
  for (int i = 1; i <= KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE; i++) {
    int delta = keyboardInterruptDeltas[(keyboardInterruptDeltasIndex - i + KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE) % KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE];
    if (delta == 255) {
      // we store any value >= 255 as 255
      Serial.print(">254");
    } else {
      Serial.print(delta);
    }
    Serial.print(" ");
  }
  Serial.println("");
  Serial.print("Partially read key code: ");
  Serial.println(incompleteKeycode, BIN);
  Serial.println("");
}

void reportKeyboardRawData() {
  if (lastReportedKeyboardInterruptCounter != keyboardInterruptCounter) {
    lastReportedKeyboardInterruptCounter = keyboardInterruptCounter;

    // display keyboard interrupt counter
    lcd.home();
    // clear line
    //        "irXXX bitXX"
    lcd.print("           ");
    lcd.home();
    lcd.print("ir");
    lcd.print(keyboardInterruptCounter);

    // display next expected bit in 0-10
    lcd.print(" bit");
    lcd.print(keyboardBitCounter);

    // display error status
    lcd.setCursor(LCD_COLS - 1, 0);
    lcd.write(keyboardErrorStatus ? keyboardErrorStatus : '_');

    // display latest bytes received in buffer
    lcd.setCursor(0, 1);
    for (int i = 1; i <= KEYBOARD_BUFFER_SIZE; i++) {
      byte value = keyboardBuffer[(keyboardBufferIndex - i + KEYBOARD_BUFFER_SIZE) % KEYBOARD_BUFFER_SIZE];
      lcd.print(value >> 4, HEX);
      lcd.print(value & 0xf, HEX);
    }

    // print last bytes received on serial in hex
    for (int i = 1; i <= KEYBOARD_BUFFER_SIZE; i++) {
      Serial.print(keyboardBuffer[(keyboardBufferIndex - i + KEYBOARD_BUFFER_SIZE) % KEYBOARD_BUFFER_SIZE], HEX);
      Serial.print(" ");
    }
    Serial.println("");

    // print last bytes received on serial in binary
    for (int i = 1; i <= KEYBOARD_BUFFER_SIZE; i++) {
      Serial.print(keyboardBuffer[(keyboardBufferIndex - i + KEYBOARD_BUFFER_SIZE) % KEYBOARD_BUFFER_SIZE], BIN);
      Serial.print(" ");
    }
    Serial.println("");
  }
}


void setup() {
  // actual keyboard stuff
  for (int i = 0; i < KEYBOARD_BUFFER_SIZE; i++) {
    keyboardBuffer[i] = 0;
  }
  for (int i = 0; i < KEYBOARD_INTERRUPT_DELTA_ARRAY_SIZE; i++) {
    keyboardInterruptDeltas[i] = 0;
  }
  pinMode(PS2_KEYBOARD_CLOCK_PIN, INPUT);
  pinMode(PS2_KEYBOARD_DATA_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PS2_KEYBOARD_CLOCK_PIN), processKeyboardInterrupt, FALLING);

  // for debug
  Serial.begin(9600);

  lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.clear();
  lcd.print("Ready");
  lcd.home();
}

void loop() {
  reportKeyboardRawData();
}
