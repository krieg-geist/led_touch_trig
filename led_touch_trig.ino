#include <FastLED.h>
#include <Adafruit_MPR121.h>
#include <Wire.h>

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

#define TOUCH_THRESH 7

#define I2C_SDA 0
#define I2C_SCL 1
#define LED_PIN 2

// cord pins
//
// Touch    OrStrp
// DO       GrnStrp
// G        Or
// +5       Grn
// DI       Brown

const uint8_t OUT_PINS[] = { 27, 26, 15, 14 };   // { 9, 10, 11, 12 };
const uint8_t TOUCH_PINS[] = { 11, 10, 9, 8 };  // { 11, 10, 9, 8 };
const uint8_t LED_OFFSETS[] = { 0, 13, 30, 43 };  // { 11, 10, 9, 8 };

const uint8_t NUM_LED_CIRCLES = 3;
const uint8_t LEDS_PER_CIRCLE[NUM_LED_CIRCLES] = { 12, 16, 12 };
const uint8_t NUM_LEDS = (12 + 16 + 12 + NUM_LED_CIRCLES - 1);

CRGB leds[NUM_LEDS];
//CRGB* leds[NUM_LED_CIRCLES];


Adafruit_MPR121 cap = Adafruit_MPR121();
uint16_t lasttouched = 0;
uint16_t currtouched = 0;


const auto FLICKER_COLOR_WHITE = CRGB(255, 255, 255);  // white

const CRGB FLICKER_COLORS[] = {
  CRGB(255, 0, 0),    // Red
  CRGB(255, 165, 0),  // Orange
  CRGB(0, 0, 255),    // Blue
  CRGB(255, 0, 128),  // Purple
  CRGB(0, 255, 128),  // Tealish
  CRGB(128, 255, 0),   // Orangeish
  CRGB(0, 255, 255),  // Cyan
  CRGB(255, 0, 255)  // Magenta
};

const int FLICKER_ATTACK_TIME = 800;
const int FLICKER_RELEASE_TIME = 1500;

const int FLICKER_RATE = 50;

const int MAX_INITIAL_BRIGHTNESS = 256;
const int MIN_INITIAL_BRIGHTNESS = 500;

const int RAND_WHITE_CHANCE = 100;

enum class FlickerState {
  Attack,
  Sustain,
  Release
};

struct ChannelGroup {
  CRGB colorA;
  CRGB colorB;
  FlickerState state;
  unsigned long startTime;
  unsigned long releaseTime;
  bool active;
  bool trigger;  // Trigger to control attack and sustain phases
  bool toggleState;
};

ChannelGroup channelGroups[NUM_LED_CIRCLES];

void updateChannelGroups() {

  for (int i = 0; i < NUM_LED_CIRCLES; i++) {
    ChannelGroup& group = channelGroups[i];
    if (!group.active) continue;

    unsigned long elapsedTime = millis() - group.startTime;
    float brightnessFactor = 0.0f;

    // Determine flicker state
    if (group.trigger) {
      if (elapsedTime < FLICKER_ATTACK_TIME) {
        group.state = FlickerState::Attack;
        brightnessFactor = min(1.0f, static_cast<float>(elapsedTime) / FLICKER_ATTACK_TIME);
      } else {
        group.state = FlickerState::Sustain;
        brightnessFactor = 1.0;
      }
    } else {
      if (group.state == FlickerState::Sustain) {
        group.releaseTime = millis();
        group.state = FlickerState::Release;
      }
      brightnessFactor = 1.0f - min(1.0f, static_cast<float>(millis() - group.releaseTime) / FLICKER_RELEASE_TIME);
      if (millis() - group.releaseTime > FLICKER_RELEASE_TIME) {
        group.active = false;
      }
    }

    for (int j = 0; j < LEDS_PER_CIRCLE[i]; j++) {
      if (group.active == false) {
        leds[LED_OFFSETS[i] + j] = CRGB(0, 0, 0);
        continue;
      }

      CRGB color = (j % 2 == 0) ? group.colorA : group.colorB;

      uint16_t maxBrightness = 0;
      uint16_t minBrightness = 0;

      switch (group.state) {
        case FlickerState::Attack:
        case FlickerState::Release:
          maxBrightness = static_cast<uint16_t>(255 * brightnessFactor);
          minBrightness = static_cast<uint16_t>(600 * brightnessFactor);
          break;
        case FlickerState::Sustain:
          if (!random(RAND_WHITE_CHANCE)) {
            leds[LED_OFFSETS[i] + j] = CRGB(255, 255, 255);
            continue;
          }
          maxBrightness = 255;
          minBrightness = 600;
          break;
      }
      uint16_t scale = random(minBrightness, 1001);  // Modified range for more flicker effect

      uint8_t r = min(255, (int)((color.r * scale * maxBrightness) / (1000 * 255)));
      uint8_t g = min(255, (int)((color.g * scale * maxBrightness) / (1000 * 255)));
      uint8_t b = min(255, (int)((color.b * scale * maxBrightness) / (1000 * 255)));
      leds[LED_OFFSETS[i] + j] = CRGB(r, g, b);
    }
  }
  FastLED.show();
}

void startFlicker(uint8_t ring) {

  if (!channelGroups[ring].active) {
    channelGroups[ring].releaseTime = 0;
    channelGroups[ring].startTime = millis();
    channelGroups[ring].active = true;
    channelGroups[ring].trigger = true;
    return;
  }
}



// LEDS
void setup() {
  Serial.begin(115200);
  delay(1000);  // give me time to bring up serial monitor
  Serial.println("init");
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);

  if (!cap.begin(0x5A)) {
    Serial.println("MPR121 not found, check wiring?");
    while (1)
      ;
  }
  Serial.println("MPR121 found!");
  // cap.setThresholds(10, 5);

  Serial.println(NUM_LEDS);
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);

  initChannelGroups();
}

uint16_t lastFlickerUpdateTime = 0;

void initChannelGroups() {
  // Initialize the flicker groups
  for (int i = 0; i < NUM_LED_CIRCLES; i++) {
    pinMode(OUT_PINS[i], OUTPUT);
    // Init channels
    channelGroups[i].active = false;
    channelGroups[i].colorA = FLICKER_COLORS[i * 2];
    channelGroups[i].colorB = FLICKER_COLORS[i * 2 + 1];
    channelGroups[i].toggleState = false;
  }
}

void loop() {
  currtouched = cap.touched();
  uint16_t changedBits = currtouched ^ lasttouched; // XOR to find changed bits
  // Check if changedBits is exactly one bit (power of two)
  if (changedBits && !(changedBits & (changedBits - 1))) {
    for (uint8_t i = 0; i < NUM_LED_CIRCLES; i++) {
      // Check for a change from not touched to touched
      if ((currtouched & _BV(TOUCH_PINS[i])) && !(lasttouched & _BV(TOUCH_PINS[i]))) {
        // Toggle the output pin state
        channelGroups[i].toggleState = !channelGroups[i].toggleState;  // Toggle the stored state
        digitalWrite(OUT_PINS[i], channelGroups[i].toggleState);       // Apply the new state to the output pin

        if (channelGroups[i].toggleState) {
          Serial.print(i);
          Serial.println(" ON");
          startFlicker(i);
        } else {
          Serial.print(i);
          Serial.println(" OFF");
          channelGroups[i].trigger = false;
        }
      }
    }
    lasttouched = currtouched;
  }

  auto current_time = millis();
  if (current_time - lastFlickerUpdateTime >= FLICKER_RATE) {
    updateChannelGroups();
    lastFlickerUpdateTime = current_time;
  }
}