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

// Display
static const uint8_t DISPLAY_WIDTH = 8;
static const uint8_t DISPLAY_HEIGHT = 32;
static const uint8_t MATRIX_SIZE = 8;

static const uint8_t MATRIX1_ADDRESS = 0x70;
static const uint8_t MATRIX2_ADDRESS = 0x72;
static const uint8_t MATRIX3_ADDRESS = 0x73;
static const uint8_t MATRIX4_ADDRESS = 0x71;

static const uint8_t MATRIX_BRIGHTNESS = 8;

// Animation
static const uint8_t MAX_FIREWORKS = 1;
static const uint8_t MAX_PARTICLES = 24;

static const uint16_t FRAME_DELAY_MS = 30;
static const float PHYSICS_FRAME_MS = 16.0f;
static const float MAX_DT = 3.0f;

// Rocket physics
static const float ROCKET_START_Y = DISPLAY_HEIGHT - 1;
static const float ROCKET_START_VELOCITY = -0.25f;
static const float ROCKET_ACCELERATION = -0.035f;

static const uint8_t ROCKET_MIN_HEIGHT = 2;
static const uint8_t ROCKET_MAX_HEIGHT = 7;

// Particle physics
static const int8_t PARTICLE_VX_MIN = -10;
static const int8_t PARTICLE_VX_MAX = 10;
static const int8_t PARTICLE_VY_MIN = -12;
static const int8_t PARTICLE_VY_MAX = 3;

static const float PARTICLE_VELOCITY_SCALE = 10.0f;
static const float PARTICLE_GRAVITY = 0.045f;

static const uint8_t PARTICLE_MIN_LIFE = 24;
static const uint8_t PARTICLE_MAX_LIFE = 40;

static const uint8_t PARTICLE_FADE_LIFE = 12;
static const uint8_t PARTICLE_FADE_RANDOM = 10;

// Top matrix column mapping
static const uint8_t TOP_COLUMN[DISPLAY_WIDTH] = {
  1, 2, 3, 3, 4, 4, 5, 6
};

Adafruit_BicolorMatrix matrix1 = Adafruit_BicolorMatrix();
Adafruit_8x8matrix matrix2 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix3 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix4 = Adafruit_8x8matrix();

GFXcanvas1 display(DISPLAY_WIDTH, DISPLAY_HEIGHT);

struct Particle {
  float x;
  float y;
  float vx;
  float vy;
  uint8_t life;
};

class Firework {
public:
  void start() {
    rocket_x = random(0, DISPLAY_WIDTH);
    rocket_y = ROCKET_START_Y;
    rocket_vy = ROCKET_START_VELOCITY;

    // Pick a random explosion height, but leave room above
    // the firework so the explosion isn't clipped at the top.
    rocket_target_y = random(
      ROCKET_MIN_HEIGHT,
      ROCKET_MAX_HEIGHT + 1
    );

    rocket_active = true;
  }

  void update(float dt) {
    if (rocket_active)
      updateRocket(dt);
    else
      updateParticles(dt);
  }

  void draw() const {
    if (rocket_active) {
      int x = constrain(
        (int)round(rocket_x),
        0,
        DISPLAY_WIDTH - 1
      );

      int y = constrain(
        (int)round(rocket_y),
        0,
        DISPLAY_HEIGHT - 1
      );

      display.drawPixel(x, y, HIGH);
      return;
    }

    for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].life)
        continue;

      int x = round(particles[i].x);
      int y = round(particles[i].y);

      if (x < 0 || x >= DISPLAY_WIDTH ||
          y < 0 || y >= DISPLAY_HEIGHT)
        continue;

      // Fade by making older particles increasingly intermittent.
      if (particles[i].life < PARTICLE_FADE_LIFE &&
          random(0, PARTICLE_FADE_RANDOM) > particles[i].life)
        continue;

      display.drawPixel(x, y, HIGH);
    }
  }

private:
  Particle particles[MAX_PARTICLES];

  bool rocket_active = false;
  float rocket_x = 0;
  float rocket_y = 0;
  float rocket_vy = 0;
  uint8_t rocket_target_y = 0;

  void updateRocket(float dt) {
    rocket_vy += ROCKET_ACCELERATION * dt;
    rocket_y += rocket_vy * dt;

    if (rocket_y <= rocket_target_y)
      explode();
  }

  void updateParticles(float dt) {
    bool particles_alive = false;

    for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].life)
        continue;

      particles_alive = true;

      particles[i].vy += PARTICLE_GRAVITY * dt;
      particles[i].x += particles[i].vx * dt;
      particles[i].y += particles[i].vy * dt;

      particles[i].life--;
    }

    if (!particles_alive)
      start();
  }

  void explode() {
    for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
      particles[i].x = rocket_x;
      particles[i].y = rocket_y;

      // Give every particle an outward velocity.
      particles[i].vx =
        random(PARTICLE_VX_MIN, PARTICLE_VX_MAX + 1) /
        PARTICLE_VELOCITY_SCALE;

      particles[i].vy =
        random(PARTICLE_VY_MIN, PARTICLE_VY_MAX + 1) /
        PARTICLE_VELOCITY_SCALE;

      particles[i].life =
        random(PARTICLE_MIN_LIFE, PARTICLE_MAX_LIFE + 1);
    }

    rocket_active = false;
  }
};

Firework fireworks[MAX_FIREWORKS];

uint32_t last_frame = 0;

void drawDisplay() {
  matrix1.clear();
  matrix2.clear();
  matrix3.clear();
  matrix4.clear();

  for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
    for (uint8_t y = 0; y < DISPLAY_HEIGHT; y++) {
      if (!display.getPixel(x, y))
        continue;

      uint8_t matrix = y / MATRIX_SIZE;
      uint8_t matrix_y = y % MATRIX_SIZE;
      uint8_t matrix_x = x;

      if (matrix == 0)
        matrix_x = TOP_COLUMN[x];

      switch (matrix) {
        case 0:
          matrix1.drawPixel(matrix_x, matrix_y, LED_YELLOW);
          break;

        case 1:
          matrix2.drawPixel(matrix_x, matrix_y, LED_ON);
          break;

        case 2:
          matrix3.drawPixel(matrix_x, matrix_y, LED_ON);
          break;

        case 3:
          matrix4.drawPixel(matrix_x, matrix_y, LED_ON);
          break;
      }
    }
  }

  matrix1.writeDisplay();
  matrix2.writeDisplay();
  matrix3.writeDisplay();
  matrix4.writeDisplay();
}

void updateFireworks(float dt) {
  for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
    fireworks[i].update(dt);
}

void drawFireworks() {
  display.fillScreen(LOW);

  for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
    fireworks[i].draw();

  drawDisplay();
}

void setup() {
  matrix1.begin(MATRIX1_ADDRESS);
  matrix2.begin(MATRIX2_ADDRESS);
  matrix3.begin(MATRIX3_ADDRESS);
  matrix4.begin(MATRIX4_ADDRESS);

  matrix1.setBrightness(MATRIX_BRIGHTNESS);
  matrix2.setBrightness(MATRIX_BRIGHTNESS);
  matrix3.setBrightness(MATRIX_BRIGHTNESS);
  matrix4.setBrightness(MATRIX_BRIGHTNESS);

  matrix2.setRotation(1);
  matrix3.setRotation(1);
  matrix4.setRotation(1);

  // matrices are stacked in column
  // matrix 1 is 8x8 bicolor matrix with larger/wider pixels on top
  // matrix2-4 are smaller 8x8 matrices stacked in a column

  // TODO: form a virtual 8x32 matrix and draw to it
  // matrix1 is larger, so if we have a dot move up, the col-to-col
  // mapping for matrix2 to matrix1 is: 1 2 3 3|4 4 5 6
  // to validate we can have a basic animation that moves a dot up each column from bottom

  // TODO: after setting up grid, do a firework animation
  // 1. a dot moves up a random column, ideally with some acceleration similar to a rocket
  // 2. when it reaches the top, it explodes into a random pattern of dots that move outwards and fade away

  // TODO: once we have a basic firework we can add more kinds of firework colors and patterns, i.e.
  // color: reg, yellow, green, or a mix of 2 or all of them, or some thing change colors
  // pattern: sparkling glittery fireworks, fireworks with straight paths,
  // speed: fast and slow, some with fast particles, some with slow

  randomSeed(analogRead(A0));

  for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
    fireworks[i].start();
}

void loop() {
  uint32_t now = millis();

  if (last_frame == 0)
    last_frame = now;

  float dt = (now - last_frame) / PHYSICS_FRAME_MS;
  last_frame = now;

  // Keep physics stable if the loop is delayed.
  if (dt > MAX_DT)
    dt = MAX_DT;

  updateFireworks(dt);
  drawFireworks();

  delay(FRAME_DELAY_MS);
}