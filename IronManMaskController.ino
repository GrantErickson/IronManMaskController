/*
 * IronManMaskController
 * --------------------------------------------------------------------------
 * Firmware for an Arduino Nano that drives a 3D-printed Iron Man helmet.
 *
 * Features (see README.md):
 *   1. Eye brightness is set live by a potentiometer dimmer.
 *   2. A push button opens / closes the faceplate via two servos.
 *   3. An optional TTP223 capacitive touch sensor also opens / closes it.
 *   4. The eyes power up when the mask closes and switch off when it opens.
 *   5. Power is controlled by the battery pack switch (no code needed).
 *   6. The eyes automatically switch off 10 minutes after they come on.
 *
 * Requires the "ServoEasing" library (install via the Library Manager).
 * --------------------------------------------------------------------------
 */

#include "ServoEasing.h"

// ----------------------------- Pin assignments ----------------------------
const uint8_t BUTTON_PIN        = 2;   // momentary push button -> GND (INPUT_PULLUP)
const uint8_t TOUCH_PIN         = 3;   // TTP223 capacitive touch, HIGH when touched
const uint8_t LED_PIN           = 6;   // eye LEDs (must be a PWM pin)
const uint8_t POT_PIN           = A0;  // brightness potentiometer
const uint8_t SERVO_TOP_PIN     = 9;
const uint8_t SERVO_BOTTOM_PIN  = 10;

// --------------------------- Servo calibration ----------------------------
// Tune these to your build. Values are servo angles in degrees.
const int TOP_OPEN       = 20;
const int TOP_CLOSED     = 167;
const int BOTTOM_OPEN    = 20;
const int BOTTOM_CLOSED  = 107;
const uint16_t SERVO_SPEED = 190;   // degrees per second

// --------------------------- Brightness mapping ---------------------------
// Raw analogRead() values (0-1023) that map to "off" and "full brightness".
// Calibrate POT_MIN/POT_MAX to the usable travel of your potentiometer.
const int POT_MIN = 250;
const int POT_MAX = 750;

// ------------------------------- Timing -----------------------------------
const unsigned long EYE_TIMEOUT_MS = 10UL * 60UL * 1000UL;  // 10 minutes
const unsigned long DEBOUNCE_MS    = 50;

ServoEasing servoTop;
ServoEasing servoBottom;

// --------------------------- Input edge helper ----------------------------
// Debounced detector that fires once each time an input becomes "active".
struct EdgeInput {
  uint8_t pin;
  bool    activeHigh;     // true: active when HIGH (touch); false: active when LOW (button)
  bool    stableState;
  bool    lastReading;
  unsigned long lastChangeMs;
};

EdgeInput button = { BUTTON_PIN, false, false, false, 0 };
EdgeInput touch  = { TOUCH_PIN,  true,  false, false, 0 };

// Returns true exactly once on the edge where the input becomes active.
bool becameActive(EdgeInput &in) {
  bool reading = ((digitalRead(in.pin) == HIGH) == in.activeHigh);
  if (reading != in.lastReading) {
    in.lastReading = reading;
    in.lastChangeMs = millis();
  }
  if ((millis() - in.lastChangeMs) >= DEBOUNCE_MS && reading != in.stableState) {
    in.stableState = reading;
    if (in.stableState) {
      return true;            // just transitioned to active
    }
  }
  return false;
}

// Seed an input's state from the current pin level so an input that is already
// active at power-up (button held, finger on the touch pad) is treated as the
// baseline and does not fire a spurious trigger right after boot.
void seedInput(EdgeInput &in) {
  bool reading = ((digitalRead(in.pin) == HIGH) == in.activeHigh);
  in.lastReading  = reading;
  in.stableState  = reading;
  in.lastChangeMs = millis();
}

// ------------------------------ Mask state --------------------------------
bool maskClosed = false;
bool eyesOn     = false;
unsigned long eyesOnAt = 0;

int readBrightness() {
  int b = map(analogRead(POT_PIN), POT_MIN, POT_MAX, 0, 255);
  return constrain(b, 0, 255);
}

void eyesOff() {
  eyesOn = false;
  analogWrite(LED_PIN, 0);
}

// Iron-Man style flicker power-up, then settle to the live brightness.
void eyesPowerUp() {
  int b = readBrightness();
  analogWrite(LED_PIN, b / 3); delay(80);
  analogWrite(LED_PIN, b / 6); delay(80);
  analogWrite(LED_PIN, b / 2); delay(80);
  analogWrite(LED_PIN, b / 4); delay(80);
  analogWrite(LED_PIN, b);     delay(80);
  eyesOn = true;
  eyesOnAt = millis();         // (re)start the 10-minute timeout
}

void openMask() {
  eyesOff();                                 // eyes go out as the faceplate lifts
  servoTop.setEaseTo(TOP_OPEN);
  servoBottom.setEaseTo(BOTTOM_OPEN);
  synchronizeAllServosStartAndWaitForAllServosToStop();
  maskClosed = false;
}

void closeMask() {
  servoTop.setEaseTo(TOP_CLOSED);
  servoBottom.setEaseTo(BOTTOM_CLOSED);
  synchronizeAllServosStartAndWaitForAllServosToStop();
  maskClosed = true;
  eyesPowerUp();                             // eyes come on once the mask is shut
}

// Handle one button press OR touch tap.
void handleTrigger() {
  if (!maskClosed) {
    closeMask();
  } else if (!eyesOn) {
    eyesPowerUp();             // eyes had timed out while closed -> wake, stay closed
  } else {
    openMask();
  }
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(POT_PIN, INPUT);
  eyesOff();

  // Let the TTP223 finish its power-up self-calibration, then capture the
  // resting state of both inputs so a held button / touched pad doesn't
  // register as a fresh edge on the first loop.
  delay(500);
  seedInput(button);
  seedInput(touch);

  // Attach at the open position so the mask boots to a known state.
  servoTop.attach(SERVO_TOP_PIN, TOP_OPEN);
  servoBottom.attach(SERVO_BOTTOM_PIN, BOTTOM_OPEN);
  setSpeedForAllServos(SERVO_SPEED);
  servoTop.setEasingType(EASE_CUBIC_IN_OUT);
  servoBottom.setEasingType(EASE_CUBIC_IN_OUT);
  maskClosed = false;
}

void loop() {
  // 1. Inputs: a button press or a touch tap toggles the mask.
  bool pressed = becameActive(button);
  bool tapped  = becameActive(touch);
  if (pressed || tapped) {
    handleTrigger();
  }

  // 2. Auto-off: eyes switch off 10 minutes after coming on (mask stays closed).
  if (eyesOn && (millis() - eyesOnAt) >= EYE_TIMEOUT_MS) {
    eyesOff();
  }

  // 3. Live dimmer: track the potentiometer while the eyes are lit.
  if (eyesOn) {
    analogWrite(LED_PIN, readBrightness());
  }
}
