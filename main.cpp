#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

static const uint8_t PROGMEM
  smile_bmp[] =
  { B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10100101,
    B10011001,
    B01000010,
    B00111100 },
  neutral_bmp[] =
  { B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10111101,
    B10000001,
    B01000010,
    B00111100 },
  frown_bmp[] =
  { B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10011001,
    B10100101,
    B01000010,
    B00111100 };

Adafruit_BicolorMatrix matrix1 = Adafruit_BicolorMatrix();
Adafruit_8x8matrix matrix2 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix3 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix4 = Adafruit_8x8matrix();

GFXcanvas1 display(8, 32);

static const uint8_t MAX_PARTICLES = 24;

struct Particle {
  float x;
  float y;
  float vx;
  float vy;
  uint8_t life;
};

Particle particles[MAX_PARTICLES];

bool rocket_active = true;
float rocket_x;
float rocket_y;
float rocket_vy;
uint8_t rocket_target_y;

uint32_t last_frame = 0;

void drawDisplay() {
  matrix1.clear();
  matrix2.clear();
  matrix3.clear();
  matrix4.clear();

  // Virtual columns 0-7 map to the wider pixels on matrix1 as:
  // 1 2 3 3|4 4 5 6
  static const uint8_t top_column[] = { 1, 2, 3, 3, 4, 4, 5, 6 };

  for (uint8_t x = 0; x < 8; x++) {
    for (uint8_t y = 0; y < 32; y++) {
      if (!display.getPixel(x, y))
        continue;

      if (y < 8)
        matrix1.drawPixel(top_column[x], y, LED_YELLOW);
      else if (y < 16)
        matrix2.drawPixel(x, y - 8, LED_ON);
      else if (y < 24)
        matrix3.drawPixel(x, y - 16, LED_ON);
      else
        matrix4.drawPixel(x, y - 24, LED_ON);
    }
  }

  matrix1.writeDisplay();
  matrix2.writeDisplay();
  matrix3.writeDisplay();
  matrix4.writeDisplay();
}

void explode() {
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    particles[i].x = rocket_x;
    particles[i].y = rocket_y;

    // Give every particle an outward velocity.
    particles[i].vx = random(-12, 13) / 10.0f;
    particles[i].vy = random(-15, 4) / 10.0f;

    particles[i].life = random(18, 32);
  }

  rocket_active = false;
}

void startRocket() {
  rocket_x = random(0, 8);
  rocket_y = 31;
  rocket_vy = -0.25f;

  // Pick a random explosion height, but leave room above
  // the firework so the explosion isn't clipped at the top.
  rocket_target_y = random(2, 6);

  rocket_active = true;
}

void updateFirework(float dt) {
  const float rocket_acceleration = -0.035f;
  const float particle_gravity = 0.055f;

  if (rocket_active) {
    rocket_vy += rocket_acceleration * dt;
    rocket_y += rocket_vy * dt;

    if (rocket_y <= rocket_target_y)
      explode();
  } else {
    bool particles_alive = false;

    for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].life)
        continue;

      particles_alive = true;

      particles[i].vy += particle_gravity * dt;
      particles[i].x += particles[i].vx * dt;
      particles[i].y += particles[i].vy * dt;

      particles[i].life--;
    }

    if (!particles_alive)
      startRocket();
  }
}

void drawFirework() {
  display.fillScreen(LOW);

  if (rocket_active) {
    int x = constrain((int)round(rocket_x), 0, 7);
    int y = constrain((int)round(rocket_y), 0, 31);
    display.drawPixel(x, y, HIGH);
  } else {
    for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].life)
        continue;

      int x = round(particles[i].x);
      int y = round(particles[i].y);

      if (x < 0 || x >= 8 || y < 0 || y >= 32)
        continue;

      // Fade by making older particles increasingly intermittent.
      if (particles[i].life < 10 && random(0, 10) > particles[i].life)
        continue;

      display.drawPixel(x, y, HIGH);
    }
  }

  drawDisplay();
}

void setup() {
  matrix1.begin(0x70);
  matrix2.begin(0x72);
  matrix3.begin(0x73);
  matrix4.begin(0x71); 
  matrix1.setBrightness(8);
  matrix2.setBrightness(8);
  matrix3.setBrightness(8);
  matrix4.setBrightness(8);
  matrix2.setRotation(1);
  matrix3.setRotation(1);
  matrix4.setRotation(1);

  // matrices are stacked in column
  // matrix 1 is 8x8 bicolor matrix with larger/wider pixels on top
  // matrix2-4 are smaller 8x8 matrices stacked in a column

  // TODO: form a virtual 8x32 matrix and draw to it
  // matrix1 is larger, so if we have a dot move up, the col-to-col
  // mapping for matrix2 to matrix1 is: 2 3 3|4 4 5 5|6 6 7
  // to validate we can have a basic animation that moves a dot up each column from bottom
  
  // TODO: after setting up grid, do a firework animation
  // 1. a dot moves up a random column, ideally with some acceleration similar to a rocket
  // 2. when it reaches the top, it explodes into a random pattern of dots that move outwards and fade away
  
  // TODO: once we have a basic firework we can add more kinds of firework colors and patterns, i.e.
  // color: reg, yellow, green, or a mix of 2 or all of them, or some thing change colors
  // pattern: sparkling glittery fireworks, fireworks with straight paths,
  // speed: fast and slow, some with fast particles, some with slow

  randomSeed(analogRead(A0));
  startRocket();
}

void loop() {
  uint32_t now = millis();

  if (last_frame == 0)
    last_frame = now;

  float dt = (now - last_frame) / 16.0f;
  last_frame = now;

  // Keep physics stable if the loop is delayed.
  if (dt > 3.0f)
    dt = 3.0f;

  updateFirework(dt);
  drawFirework();

  delay(30);
}