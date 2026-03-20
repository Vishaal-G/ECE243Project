#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PIXEL_BUF_CTRL_BASE 0xFF203020
#define PS2_BASE 0xFF200100

#define SCREEN_W 320
#define SCREEN_H 240

#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define YELLOW 0xFFE0

volatile int* pixel_ctrl_ptr = (int*)PIXEL_BUF_CTRL_BASE;
volatile int* PS2_ptr = (int*)PS2_BASE;

int pixel_buffer_start;

// Keyboard state 
bool key_w = false;
bool key_s = false;
bool key_a = false;
bool key_d = false;
bool key_r = false;

bool break_code = false;

// Car state 
float car_x = 160.0f;
float car_y = 120.0f;
float car_angle = 1.5708f;  
float car_speed = 0.0f;

// Physics 
const float accel_forward = 0.08f;
const float accel_reverse = 0.05f;
const float friction = 0.94f;
const float max_forward = 1.5f;
const float max_reverse = -0.8f;
const float turn_speed = 0.04f;

// Function prototypes 
void wait_for_vsync(void);
void plot_pixel(int x, int y, short int color);
void clear_screen(void);
void draw_line(int x0, int y0, int x1, int y1, short int color);
void draw_car(float x, float y, float angle);
void erase_car(float x, float y, float angle);
void process_keyboard_ps2(void);
void handle_keyboard_byte(unsigned char byte);
void update_key_state(unsigned char scan, bool pressed);
void reset_car(void);

// Main game loop
int main(void) {
  pixel_buffer_start = *pixel_ctrl_ptr;

  clear_screen();
  draw_car(car_x, car_y, car_angle);

  while (1) {
    float old_x = car_x;
    float old_y = car_y;
    float old_angle = car_angle;

    process_keyboard_ps2();

	// Update car physics based on input
    if (key_a) car_angle += turn_speed;
    if (key_d) car_angle -= turn_speed;

    if (key_w) car_speed += accel_forward;
    if (key_s) car_speed -= accel_reverse;

    if (car_speed > max_forward) car_speed = max_forward;
    if (car_speed < max_reverse) car_speed = max_reverse;

    car_speed *= friction;

	// Reset car
    if (key_r) {
      reset_car();
    } else {
      car_x += car_speed * cosf(car_angle);
      car_y -= car_speed * sinf(car_angle);
    }

    if (car_x < 8) car_x = 8;
    if (car_x > SCREEN_W - 9) car_x = SCREEN_W - 9;
    if (car_y < 8) car_y = 8;
    if (car_y > SCREEN_H - 9) car_y = SCREEN_H - 9;

    erase_car(old_x, old_y, old_angle);
    draw_car(car_x, car_y, car_angle);

    wait_for_vsync();
  }

  return 0;
}

// Reset the car to the center of the screen with default angle and speed
void reset_car(void) {
  car_x = 160.0f;
  car_y = 120.0f;
  car_angle = 1.5708f;
  car_speed = 0.0f;
}

// Process keyboard input from PS/2 controller
void process_keyboard_ps2(void) {
  int data;

  // Read all available bytes from the PS/2 controller
  while (1) {
    data = *PS2_ptr;
    if ((data & 0x8000) == 0) break;

    // Extract the byte and handle it
    unsigned char byte = (unsigned char)(data & 0xFF);
    handle_keyboard_byte(byte);
  }
}

// Handle a single byte from the PS/2 controller
void handle_keyboard_byte(unsigned char byte) {
  if (byte == 0xF0) {
    break_code = true;
    return;
  }

  update_key_state(byte, !break_code);
  break_code = false;
}


// Update key state 
void update_key_state(unsigned char scan, bool pressed) {
  switch (scan) {
    case 0x1D:
      key_w = pressed; // W key
      break;
    case 0x1B:
      key_s = pressed; // S key
      break;
    case 0x1C:
      key_a = pressed; // A key
      break;
    case 0x23:
      key_d = pressed; // D key
      break;
    case 0x2D:
      key_r = pressed; // R key
      break;
    default:
      break;
  }
}

// Wait for vertical sync and update pixel buffer start address
void wait_for_vsync(void) {
  *pixel_ctrl_ptr = 1;
  while ((*(pixel_ctrl_ptr + 3) & 0x01) != 0);
  pixel_buffer_start = *pixel_ctrl_ptr;
}

// Plot a pixel at (x, y) with the specified color
void plot_pixel(int x, int y, short int color) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;

  volatile short int* addr =
      (volatile short int*)(pixel_buffer_start + (y << 10) + (x << 1));

  *addr = color;
}

// Clear the entire screen by plotting black pixels
void clear_screen(void) {
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      plot_pixel(x, y, BLACK);
    }
  }
}

// Draw a line from (x0, y0) to (x1, y1) using Bresenham's algorithm
void draw_line(int x0, int y0, int x1, int y1, short int color) {
  int is_steep = (abs(y1 - y0) > abs(x1 - x0));

  if (is_steep) {
    int temp;
    temp = x0;
    x0 = y0;
    y0 = temp;
    temp = x1;
    x1 = y1;
    y1 = temp;
  }

  if (x0 > x1) {
    int temp;
    temp = x0;
    x0 = x1;
    x1 = temp;
    temp = y0;
    y0 = y1;
    y1 = temp;
  }

  int dx = x1 - x0;
  int dy = abs(y1 - y0);
  int error = -(dx / 2);
  int y = y0;
  int y_step = (y0 < y1) ? 1 : -1;

  for (int x = x0; x <= x1; x++) {
    if (is_steep)
      plot_pixel(y, x, color);
    else
      plot_pixel(x, y, color);

    error += dy;
    if (error >= 0) {
      y += y_step;
      error -= dx;
    }
  }
}

// Draw the car as a rectangle with a line indicating direction
void draw_car(float x, float y, float angle) {
  int cx = (int)x;
  int cy = (int)y;

  for (int dy = -4; dy <= 4; dy++) {
    for (int dx = -6; dx <= 6; dx++) {
      plot_pixel(cx + dx, cy + dy, WHITE);
    }
  }

  int hx = cx + (int)(12.0f * cosf(angle));
  int hy = cy - (int)(12.0f * sinf(angle));
  draw_line(cx, cy, hx, hy, RED);

  plot_pixel(cx, cy, YELLOW);
}

// Erase the car by drawing a black rectangle and line over its previous position
void erase_car(float x, float y, float angle) {
  int cx = (int)x;
  int cy = (int)y;

  for (int dy = -6; dy <= 6; dy++) {
    for (int dx = -14; dx <= 14; dx++) {
      plot_pixel(cx + dx, cy + dy, BLACK);
    }
  }

  int hx = cx + (int)(12.0f * cosf(angle));
  int hy = cy - (int)(12.0f * sinf(angle));
  draw_line(cx, cy, hx, hy, BLACK);
}
