#include <Arduino.h>
#include <avr/pgmspace.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

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

static const uint8_t MAX_OUTER_PARTICLES = 24;
static const uint8_t MAX_INNER_PARTICLES = 12;
static const uint8_t MAX_CORE_PARTICLES = 8;

static const uint16_t FRAME_DELAY_MS = 40;
static const float PHYSICS_FRAME_MS = 16.0f;
static const float MAX_DT = 3.0f;

// Firework status LEDs
static const uint32_t FIREWORK_STARTUP_MS = 1000;
static const uint16_t FIREWORK_PAUSE_MS = 200;
//static const uint8_t FIREWORK_STATUS_1_PIN = 4; // controlled by 16u2 firmware
//static const uint8_t FIREWORK_STATUS_2_PIN = 5;
static const uint8_t FIREWORK_STATUS_3_PIN = LED_BUILTIN;

// Rocket
static const float ROCKET_START_Y = DISPLAY_HEIGHT - 1;
static const float ROCKET_START_VELOCITY = -0.25f;
static const float ROCKET_ACCELERATION = -0.035f;
static const uint8_t ROCKET_EXPLODE_MIN_HEIGHT = 2;
static const uint8_t ROCKET_EXPLODE_MAX_HEIGHT = 5;

// Particle direction
static const int8_t PARTICLE_DIRECTION_MIN = -100;
static const int8_t PARTICLE_DIRECTION_MAX = 100;
static const float PARTICLE_DIRECTION_SCALE = 100.0f;

// Particle fade
static const uint8_t OUTER_FADE_LIFE = 12;
static const uint8_t OUTER_FADE_RANDOM = 10;

static const uint8_t INNER_FADE_LIFE = 10;
static const uint8_t INNER_FADE_RANDOM = 8;

static const uint8_t CORE_FADE_LIFE = 4;
static const uint8_t CORE_FADE_RANDOM = 4;

// Top matrix column mapping.
static const uint8_t TOP_COLUMN[DISPLAY_WIDTH] PROGMEM = {
  1, 2, 3, 3, 4, 4, 5, 6
};

// Firework styles
struct FireworkStyle {
  uint8_t color;

  // Outer particles
  float outer_speed_min;
  float outer_speed_max;
  float outer_gravity;
  float outer_drag;
  uint8_t outer_life_min;
  uint8_t outer_life_max;

  // Middle particles
  float inner_speed_min;
  float inner_speed_max;
  float inner_gravity;
  float inner_drag;
  uint8_t inner_life_min;
  uint8_t inner_life_max;

  // Core particles
  float core_speed_min;
  float core_speed_max;
  float core_gravity;
  uint8_t core_life_min;
  uint8_t core_life_max;
};

static const FireworkStyle FIREWORK_STYLES[] PROGMEM = {
  // Classic
  {
    LED_RED,

    // Outer
    0.45f,
    0.70f,
    0.025f,
    0.980f,
    24,
    40,

    // Middle
    0.28f,
    0.45f,
    0.015f,
    0.975f,
    18,
    30,

    // Core
    0.04f,
    0.07f,
    0.001f,
    6,
    10
  },

  // Wide
  {
    LED_YELLOW,

    // Outer
    0.55f,
    0.80f,
    0.030f,
    0.985f,
    20,
    34,

    // Middle
    0.32f,
    0.50f,
    0.018f,
    0.980f,
    16,
    28,

    // Core
    0.05f,
    0.08f,
    0.001f,
    6,
    9
  },

  // Floaty
  {
    LED_GREEN,

    // Outer
    0.25f,
    0.45f,
    0.018f,
    0.955f,
    35,
    55,

    // Middle
    0.16f,
    0.30f,
    0.010f,
    0.950f,
    24,
    38,

    // Core
    0.03f,
    0.06f,
    0.0005f,
    7,
    10
  }
};

static const uint8_t FIREWORK_STYLE_COUNT =
  sizeof(FIREWORK_STYLES) / sizeof(FIREWORK_STYLES[0]);

Adafruit_BicolorMatrix matrix1 = Adafruit_BicolorMatrix();
Adafruit_8x8matrix matrix2 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix3 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix4 = Adafruit_8x8matrix();

GFXcanvas1 display(DISPLAY_WIDTH, DISPLAY_HEIGHT);

uint8_t display_color[MATRIX_SIZE][DISPLAY_WIDTH];

struct Particle {
  float x;
  float y;
  float vx;
  float vy;
  uint8_t life;
};

class Firework {
public:
  enum State {
    WAITING,
    ROCKET,
    PARTICLES
  };

  void start() {
    // RAM NOTE: bulk-copy one style record out of flash (PROGMEM) into this instance's
    // own RAM-resident storage. Every existing "style->field" access below is unchanged -
    // only this assignment and the style_storage/style member declarations differ from
    // the pointer-into-a-RAM-resident-table version.
    memcpy_P(
      &style_storage,
      &FIREWORK_STYLES[random(0, FIREWORK_STYLE_COUNT)],
      sizeof(FireworkStyle)
    );

    rocket_x = random(0, DISPLAY_WIDTH);
    rocket_y = ROCKET_START_Y;
    rocket_vy = ROCKET_START_VELOCITY;

    // Pick a random explosion height, but leave room above
    // the firework so the explosion isn't clipped at the top.
    rocket_target_y = random(
      ROCKET_EXPLODE_MIN_HEIGHT,
      ROCKET_EXPLODE_MAX_HEIGHT + 1
    );

    state = ROCKET;
  }

  void update(float dt) {
    if (state == WAITING)
      updateWaiting(dt);
    else if (state == ROCKET)
      updateRocket(dt);
    else
      updateParticles(dt);
  }

  void draw() const {
    if (state == WAITING)
      return;

    if (state == ROCKET) {
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

      if (y < MATRIX_SIZE)
        display_color[y][x] = style->color;

      return;
    }

    // Outer explosion particles.
    for (uint8_t i = 0; i < MAX_OUTER_PARTICLES; i++) {
      if (!outer_particles[i].life)
        continue;

      drawParticle(
        outer_particles[i],
        OUTER_FADE_LIFE,
        OUTER_FADE_RANDOM
      );
    }

    // Middle particles fill the space between the core and outer shell.
    for (uint8_t i = 0; i < MAX_INNER_PARTICLES; i++) {
      if (!inner_particles[i].life)
        continue;

      drawParticle(
        inner_particles[i],
        INNER_FADE_LIFE,
        INNER_FADE_RANDOM
      );
    }

    // Slow core particles.
    for (uint8_t i = 0; i < MAX_CORE_PARTICLES; i++) {
      if (!core_particles[i].life)
        continue;

      drawParticle(
        core_particles[i],
        CORE_FADE_LIFE,
        CORE_FADE_RANDOM
      );
    }
  }

  bool isRocket() const {
    return state == ROCKET;
  }

  bool isParticles() const {
    return state == PARTICLES;
  }

  bool isRocketFirstHalf() const {
    if (state != ROCKET)
      return false;

    float total_distance = ROCKET_START_Y - rocket_target_y;
    float distance = ROCKET_START_Y - rocket_y;

    return distance < total_distance * 0.5f;
  }

private:
  Particle outer_particles[MAX_OUTER_PARTICLES];
  Particle inner_particles[MAX_INNER_PARTICLES];
  Particle core_particles[MAX_CORE_PARTICLES];

  FireworkStyle style_storage{};
  const FireworkStyle *style = &style_storage;

  State state = WAITING;

  float rocket_x = 0;
  float rocket_y = 0;
  float rocket_vy = 0;
  uint8_t rocket_target_y = 0;

  float wait_remaining_ms = 0;

  void updateWaiting(float dt) {
    wait_remaining_ms -= dt * PHYSICS_FRAME_MS;

    if (wait_remaining_ms <= 0)
      start();
  }

  void updateRocket(float dt) {
    rocket_vy += ROCKET_ACCELERATION * dt;
    rocket_y += rocket_vy * dt;

    if (rocket_y <= rocket_target_y)
      explode();
  }

  void updateParticles(float dt) {
    bool particles_alive = false;

    // Outer particles.
    for (uint8_t i = 0; i < MAX_OUTER_PARTICLES; i++) {
      if (!outer_particles[i].life)
        continue;

      particles_alive = true;

      outer_particles[i].vx *= style->outer_drag;
      outer_particles[i].vy *= style->outer_drag;

      outer_particles[i].vy += style->outer_gravity * dt;
      outer_particles[i].x += outer_particles[i].vx * dt;
      outer_particles[i].y += outer_particles[i].vy * dt;

      outer_particles[i].life--;
    }

    // Middle particles.
    for (uint8_t i = 0; i < MAX_INNER_PARTICLES; i++) {
      if (!inner_particles[i].life)
        continue;

      particles_alive = true;

      inner_particles[i].vx *= style->inner_drag;
      inner_particles[i].vy *= style->inner_drag;

      inner_particles[i].vy += style->inner_gravity * dt;
      inner_particles[i].x += inner_particles[i].vx * dt;
      inner_particles[i].y += inner_particles[i].vy * dt;

      inner_particles[i].life--;
    }

    // Core particles.
    for (uint8_t i = 0; i < MAX_CORE_PARTICLES; i++) {
      if (!core_particles[i].life)
        continue;

      particles_alive = true;

      core_particles[i].vy += style->core_gravity * dt;
      core_particles[i].x += core_particles[i].vx * dt;
      core_particles[i].y += core_particles[i].vy * dt;

      core_particles[i].life--;
    }

    if (!particles_alive) {
      state = WAITING;
      wait_remaining_ms = FIREWORK_PAUSE_MS;
    }
  }

  void explode() {
    // Outer particles.
    for (uint8_t i = 0; i < MAX_OUTER_PARTICLES; i++) {
      initializeParticle(
        outer_particles[i],
        style->outer_speed_min,
        style->outer_speed_max,
        style->outer_life_min,
        style->outer_life_max
      );
    }

    // Middle particles.
    for (uint8_t i = 0; i < MAX_INNER_PARTICLES; i++) {
      initializeParticle(
        inner_particles[i],
        style->inner_speed_min,
        style->inner_speed_max,
        style->inner_life_min,
        style->inner_life_max
      );
    }

    // Core particles.
    for (uint8_t i = 0; i < MAX_CORE_PARTICLES; i++) {
      initializeParticle(
        core_particles[i],
        style->core_speed_min,
        style->core_speed_max,
        style->core_life_min,
        style->core_life_max
      );
    }

    state = PARTICLES;
  }

  void initializeParticle(
    Particle &particle,
    float speed_min,
    float speed_max,
    uint8_t life_min,
    uint8_t life_max
  ) {
    particle.x = rocket_x;
    particle.y = rocket_y;

    float dx;
    float dy;
    float magnitude;

    do {
      dx = random(
        PARTICLE_DIRECTION_MIN,
        PARTICLE_DIRECTION_MAX + 1
      ) / PARTICLE_DIRECTION_SCALE;

      dy = random(
        PARTICLE_DIRECTION_MIN,
        PARTICLE_DIRECTION_MAX + 1
      ) / PARTICLE_DIRECTION_SCALE;

      magnitude = sqrt(dx * dx + dy * dy);
    } while (magnitude < 0.2f);

    float speed = random(
      (int)(speed_min * 100.0f),
      (int)(speed_max * 100.0f) + 1
    ) / 100.0f;

    particle.vx = dx / magnitude * speed;
    particle.vy = dy / magnitude * speed;

    particle.life = random(
      life_min,
      life_max + 1
    );
  }

  void drawParticle(
    const Particle &particle,
    uint8_t fade_life,
    uint8_t fade_random
  ) const {
    int x = round(particle.x);
    int y = round(particle.y);

    if (x < 0 || x >= DISPLAY_WIDTH ||
        y < 0 || y >= DISPLAY_HEIGHT)
      return;

    // Fade by making older particles increasingly intermittent.
    if (particle.life < fade_life &&
        random(0, fade_random) > particle.life)
      return;

    display.drawPixel(x, y, HIGH);

    if (y < MATRIX_SIZE)
      display_color[y][x] = style->color;
  }
};

Firework fireworks[MAX_FIREWORKS];

uint32_t last_frame = 0;
uint32_t startup_time = 0;
bool animation_started = false;
bool last_status_rx = false;
bool last_status_tx = false;
bool last_status_led = false;
bool status_leds_initialized = false;

void setStatusLEDs(bool rx, bool tx, bool led) {
  // communicate over serial to custom 16u2 firmware to control TX/RX LEDs
  Serial.write(0xA0 | (tx << 2) | rx);
  digitalWrite(FIREWORK_STATUS_3_PIN, led ? HIGH : LOW);
}

void updateStatusLEDs() {
  bool rx = false;
  bool tx = false;
  bool led = false;

  // Only show status of primary firework.
  if (fireworks[0].isRocket()) {
    if (fireworks[0].isRocketFirstHalf())
      rx = true;
    else
      tx = true;
  } else if (fireworks[0].isParticles()) {
    led = true;
  }

  if (!status_leds_initialized ||
      rx != last_status_rx ||
      tx != last_status_tx ||
      led != last_status_led) {
    setStatusLEDs(rx, tx, led);

    last_status_rx = rx;
    last_status_tx = tx;
    last_status_led = led;
    status_leds_initialized = true;
  }
}

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
        matrix_x = pgm_read_byte(&TOP_COLUMN[x]);

      switch (matrix) {
        case 0:
          matrix1.drawPixel(
            matrix_x,
            matrix_y,
            display_color[y][x]
          );
          break;

        case 1:
          matrix2.drawPixel(
            matrix_x,
            matrix_y,
            LED_ON
          );
          break;

        case 2:
          matrix3.drawPixel(
            matrix_x,
            matrix_y,
            LED_ON
          );
          break;

        case 3:
          matrix4.drawPixel(
            matrix_x,
            matrix_y,
            LED_ON
          );
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

  for (uint8_t y = 0; y < MATRIX_SIZE; y++) {
    for (uint8_t x = 0; x < DISPLAY_WIDTH; x++)
      display_color[y][x] = LED_OFF;
  }

  for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
    fireworks[i].draw();

  drawDisplay();
}

void setup() {
  Serial.begin(9600);

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

  // Do not touch RX/TX/LED_BUILTIN during startup.
  // This gives the bootloader/programming interface time
  // to finish before these pins become status outputs.
  startup_time = millis();

  display.fillScreen(LOW);

  for (uint8_t y = 0; y < MATRIX_SIZE; y++) {
    for (uint8_t x = 0; x < DISPLAY_WIDTH; x++)
      display_color[y][x] = LED_OFF;
  }

  drawDisplay();
}

void loop() {
  uint32_t now = millis();

  // Leave the programming/status pins alone for a few seconds
  // after reset so uploading/reprogramming is not disturbed.
  if (!animation_started) {
    if (now - startup_time < FIREWORK_STARTUP_MS) {
      delay(FRAME_DELAY_MS);
      return;
    }

    pinMode(FIREWORK_STATUS_3_PIN, OUTPUT);

    setStatusLEDs(false, false, false);

    for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
      fireworks[i].start();

    animation_started = true;
    last_frame = now;
  }

  float dt = (now - last_frame) / PHYSICS_FRAME_MS;
  last_frame = now;

  // Keep physics stable if the loop is delayed.
  if (dt > MAX_DT)
    dt = MAX_DT;

  updateFireworks(dt);
  updateStatusLEDs();
  drawFireworks();

  delay(FRAME_DELAY_MS);
}
