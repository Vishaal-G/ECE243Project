#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PIXEL_BUF_CTRL_BASE 0xFF203020
#define PS2_BASE 0xFF200100
#define HEX_DISPLAY_BASE 0xFF200020

#define SCREEN_W 320
#define SCREEN_H 240

#define MAP_COLS 64
#define MAP_ROWS 48
#define TILE_SIZE 32
#define WORLD_W (MAP_COLS * TILE_SIZE)
#define WORLD_H (MAP_ROWS * TILE_SIZE)

#define CAR_WIDTH 20.0f
#define CAR_LENGTH 30.0f
#define CAR_COLLISION_RADIUS 12.0f
#define CAR_TO_CAR_RADIUS 16.0f

#define PI 3.14159265f

#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define GRAY 0x8410
#define DARK_GRAY 0x4208
#define LIGHT_GRAY 0xC618
#define YELLOW 0xFFE0
#define BROWN 0x79E0
#define DARK_GREEN 0x0320

typedef enum {
  TILE_GRASS = 0,
  TILE_ROAD = 1,
  TILE_BUILDING = 2,
  TILE_BORDER = 3
} TileType;

typedef struct {
  float x;
  float y;
  float angle;
  float speed;
} Player;

typedef struct {
  float x;
  float y;
  float angle;
  float speed;
  bool active;
} PoliceCar;

volatile int* pixel_ctrl_ptr = (int*)PIXEL_BUF_CTRL_BASE;
volatile int* PS2_ptr = (int*)PS2_BASE;
volatile int* hex_ptr = (int*)HEX_DISPLAY_BASE;

volatile int pixel_buffer_start;
short int Buffer1[240][512];
short int Buffer2[240][512];

static TileType world_map[MAP_ROWS][MAP_COLS];

static Player player;
static PoliceCar police_car;
static float camera_x = 0.0f;
static float camera_y = 0.0f;

static bool key_w = false;
static bool key_s = false;
static bool key_a = false;
static bool key_d = false;
static bool key_r = false;
static bool break_code = false;
static bool extended_code = false;

static unsigned int key_press_count = 0;

static unsigned int rng_state = 0x12345678u;

static const float accel_forward = 0.42f;
static const float accel_reverse = 0.24f;
static const float grass_drag = 0.94f;
static const float road_drag = 0.985f;
static const float idle_drag = 0.985f;
static const float brake_drag = 0.88f;
static const float max_forward_speed = 7.5f;
static const float max_reverse_speed = -3.6f;
static const float turn_rate = 0.050f;
static const float police_accel = 0.22f;
static const float police_drag = 0.97f;
static const float police_max_speed = 5.0f;
static const float police_turn_rate = 0.090f;

void init_buffers(void);
void wait_for_vsync(void);
void swap_buffers(void);

void plot_pixel(int x, int y, short int color);
void clear_screen(short int color);
void draw_rect(int x, int y, int width, int height, short int color);
void draw_line(int x0, int y0, int x1, int y1, short int color);

void display_hex_digit(unsigned int digit_index, unsigned int value);
void display_hex_value(unsigned int value);

void seed_rng(unsigned int seed);
unsigned int rand_u32(void);
int rand_range(int low, int high);

void generate_map(void);
void add_road_row(int row);
void add_road_col(int col);
void place_buildings(void);

void reset_player(void);
void spawn_police_car(void);
void process_keyboard_ps2(void);
void handle_keyboard_byte(unsigned char byte);
void update_key_state(unsigned char scan, bool pressed);

bool is_blocking_tile(int col, int row);
TileType get_tile_at_world(float world_x, float world_y);
bool check_collision(float next_x, float next_y);
bool is_road_tile(int col, int row);
bool check_player_police_collision(void);

void update_player(void);
void update_police_car(void);
void update_camera(void);

void draw_world(void);
void draw_tile(int col, int row, int screen_x, int screen_y);
void draw_player(void);
void draw_police_car(void);
void draw_filled_car(float world_x, float world_y, float angle, short int body_color,
                     short int nose_color);

int clamp_int(int value, int min_value, int max_value);
float clamp_float(float value, float min_value, float max_value);
int world_to_screen_x(float world_x);
int world_to_screen_y(float world_y);

int main(void) {
  init_buffers();
  display_hex_value(0x000000);
  seed_rng(0x24324324u);
  generate_map();
  reset_player();
  spawn_police_car();
  update_camera();

  while (1) {
    process_keyboard_ps2();

    if (key_r) {
      reset_player();
      spawn_police_car();
    } else {
      update_player();
      update_police_car();
      check_player_police_collision();
    }

    update_camera();
    draw_world();
    swap_buffers();
  }

  return 0;
}

void init_buffers(void) {
  *(pixel_ctrl_ptr + 1) = (int)&Buffer1;
  wait_for_vsync();
  pixel_buffer_start = *pixel_ctrl_ptr;
  clear_screen(BLACK);

  *(pixel_ctrl_ptr + 1) = (int)&Buffer2;
  pixel_buffer_start = *(pixel_ctrl_ptr + 1);
  clear_screen(BLACK);
}

void wait_for_vsync(void) {
  *pixel_ctrl_ptr = 1;
  while ((*(pixel_ctrl_ptr + 3) & 0x01) != 0) {
  }
}

void swap_buffers(void) {
  wait_for_vsync();
  pixel_buffer_start = *(pixel_ctrl_ptr + 1);
}

void plot_pixel(int x, int y, short int color) {
  volatile short int* pixel_addr;

  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
    return;
  }

  pixel_addr = (volatile short int*)(pixel_buffer_start + (y << 10) + (x << 1));
  *pixel_addr = color;
}

void clear_screen(short int color) {
  int y;
  int x;

  for (y = 0; y < SCREEN_H; y++) {
    for (x = 0; x < SCREEN_W; x++) {
      plot_pixel(x, y, color);
    }
  }
}

void draw_rect(int x, int y, int width, int height, short int color) {
  int start_x = clamp_int(x, 0, SCREEN_W);
  int start_y = clamp_int(y, 0, SCREEN_H);
  int end_x = clamp_int(x + width, 0, SCREEN_W);
  int end_y = clamp_int(y + height, 0, SCREEN_H);
  int row;
  int col;

  for (row = start_y; row < end_y; row++) {
    for (col = start_x; col < end_x; col++) {
      plot_pixel(col, row, color);
    }
  }
}

void draw_line(int x0, int y0, int x1, int y1, short int color) {
  int steep = (abs(y1 - y0) > abs(x1 - x0));
  int temp;
  int dx;
  int dy;
  int error;
  int y_step;
  int x;

  if (steep) {
    temp = x0;
    x0 = y0;
    y0 = temp;
    temp = x1;
    x1 = y1;
    y1 = temp;
  }

  if (x0 > x1) {
    temp = x0;
    x0 = x1;
    x1 = temp;
    temp = y0;
    y0 = y1;
    y1 = temp;
  }

  dx = x1 - x0;
  dy = abs(y1 - y0);
  error = -(dx / 2);
  y_step = (y0 < y1) ? 1 : -1;

  for (x = x0; x <= x1; x++) {
    if (steep) {
      plot_pixel(y0, x, color);
    } else {
      plot_pixel(x, y0, color);
    }

    error += dy;
    if (error >= 0) {
      y0 += y_step;
      error -= dx;
    }
  }
}

void display_hex_digit(unsigned int digit_index, unsigned int value) {
  unsigned int current_value = *hex_ptr;
  unsigned int mask = 0xF << (digit_index * 4);
  value &= 0xF;
  current_value = (current_value & ~mask) | (value << (digit_index * 4));
  *hex_ptr = current_value;
}

void display_hex_value(unsigned int value) {
  *hex_ptr = value & 0xFFFFFF;
}

void seed_rng(unsigned int seed) {
  rng_state = seed;
}

unsigned int rand_u32(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state;
}

int rand_range(int low, int high) {
  unsigned int span = (unsigned int)(high - low + 1);
  return low + (int)(rand_u32() % span);
}

void generate_map(void) {
  int row;
  int col;
  int main_row;
  int main_col;

  for (row = 0; row < MAP_ROWS; row++) {
    for (col = 0; col < MAP_COLS; col++) {
      if (row == 0 || col == 0 || row == MAP_ROWS - 1 || col == MAP_COLS - 1) {
        world_map[row][col] = TILE_BORDER;
      } else {
        world_map[row][col] = TILE_GRASS;
      }
    }
  }

  main_row = MAP_ROWS / 2;
  main_col = MAP_COLS / 2;
  add_road_row(main_row);
  add_road_col(main_col);

  for (row = 6; row < MAP_ROWS - 6; row += 8) {
    add_road_row(row + rand_range(-1, 1));
  }

  for (col = 6; col < MAP_COLS - 6; col += 8) {
    add_road_col(col + rand_range(-1, 1));
  }

  place_buildings();

  for (row = 1; row < 4; row++) {
    for (col = 1; col < 4; col++) {
      world_map[row][col] = TILE_ROAD;
    }
  }
}

void add_road_row(int row) {
  int col;

  row = clamp_int(row, 1, MAP_ROWS - 2);
  for (col = 1; col < MAP_COLS - 1; col++) {
    world_map[row][col] = TILE_ROAD;
    if (row + 1 < MAP_ROWS - 1) {
      world_map[row + 1][col] = TILE_ROAD;
    }
  }
}

void add_road_col(int col) {
  int row;

  col = clamp_int(col, 1, MAP_COLS - 2);
  for (row = 1; row < MAP_ROWS - 1; row++) {
    world_map[row][col] = TILE_ROAD;
    if (col + 1 < MAP_COLS - 1) {
      world_map[row][col + 1] = TILE_ROAD;
    }
  }
}

void place_buildings(void) {
  int row;
  int col;
  int block_h;
  int block_w;
  int r;
  int c;
  bool can_place;

  for (row = 2; row < MAP_ROWS - 4; row++) {
    for (col = 2; col < MAP_COLS - 4; col++) {
      if (world_map[row][col] != TILE_GRASS) {
        continue;
      }

      if ((rand_u32() & 7u) != 0u) {
        continue;
      }

      block_h = rand_range(2, 4);
      block_w = rand_range(2, 4);
      can_place = true;

      for (r = row - 1; r <= row + block_h; r++) {
        for (c = col - 1; c <= col + block_w; c++) {
          if (r <= 0 || c <= 0 || r >= MAP_ROWS - 1 || c >= MAP_COLS - 1 ||
              world_map[r][c] != TILE_GRASS) {
            can_place = false;
          }
        }
      }

      if (!can_place) {
        continue;
      }

      for (r = row; r < row + block_h; r++) {
        for (c = col; c < col + block_w; c++) {
          world_map[r][c] = TILE_BUILDING;
        }
      }
    }
  }
}

void reset_player(void) {
  player.x = 1.5f * TILE_SIZE;
  player.y = 1.5f * TILE_SIZE;
  player.angle = PI / 2.0f;
  player.speed = 0.0f;
}

void spawn_police_car(void) {
  int attempts;
  int col;
  int row;
  float spawn_x;
  float spawn_y;
  float dx;
  float dy;

  police_car.active = false;
  police_car.speed = 0.0f;
  police_car.angle = PI / 2.0f;

  for (attempts = 0; attempts < 256; attempts++) {
    col = rand_range(1, MAP_COLS - 2);
    row = rand_range(1, MAP_ROWS - 2);

    if (!is_road_tile(col, row)) {
      continue;
    }

    spawn_x = (col + 0.5f) * TILE_SIZE;
    spawn_y = (row + 0.5f) * TILE_SIZE;
    dx = spawn_x - player.x;
    dy = spawn_y - player.y;

    if (dx * dx + dy * dy < 260.0f * 260.0f) {
      continue;
    }

    police_car.x = spawn_x;
    police_car.y = spawn_y;
    police_car.angle = atan2f(-(player.y - police_car.y), player.x - police_car.x);
    police_car.speed = 0.0f;
    police_car.active = true;
    return;
  }

  police_car.x = (MAP_COLS - 3.0f) * TILE_SIZE;
  police_car.y = (MAP_ROWS - 3.0f) * TILE_SIZE;
  police_car.angle = PI;
  police_car.speed = 0.0f;
  police_car.active = true;
}

void process_keyboard_ps2(void) {
  int data;

  while (1) {
    data = *PS2_ptr;
    if ((data & 0x8000) == 0) {
      break;
    }

    handle_keyboard_byte((unsigned char)(data & 0xFF));
  }
}

void handle_keyboard_byte(unsigned char byte) {
  if (byte == 0xE0) {
    extended_code = true;
    return;
  }

  if (byte == 0xF0) {
    break_code = true;
    return;
  }

  if (!extended_code) {
    update_key_state(byte, !break_code);
  }

  break_code = false;
  extended_code = false;
}

void update_key_state(unsigned char scan, bool pressed) {
  switch (scan) {
    case 0x1D:
      key_w = pressed;
      break;
    case 0x1B:
      key_s = pressed;
      break;
    case 0x1C:
      key_a = pressed;
      break;
    case 0x23:
      key_d = pressed;
      break;
    case 0x2D:
      key_r = pressed;
      break;
    default:
      break;
  }

  if (pressed) {
    key_press_count++;
    display_hex_value(key_press_count);
  }
}

bool is_blocking_tile(int col, int row) {
  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return true;
  }

  return world_map[row][col] == TILE_BUILDING || world_map[row][col] == TILE_BORDER;
}

bool is_road_tile(int col, int row) {
  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return false;
  }

  return world_map[row][col] == TILE_ROAD;
}

TileType get_tile_at_world(float world_x, float world_y) {
  int col = (int)(world_x / TILE_SIZE);
  int row = (int)(world_y / TILE_SIZE);

  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return TILE_BORDER;
  }

  return world_map[row][col];
}

bool check_collision(float next_x, float next_y) {
  int min_col = (int)((next_x - CAR_COLLISION_RADIUS) / TILE_SIZE);
  int max_col = (int)((next_x + CAR_COLLISION_RADIUS) / TILE_SIZE);
  int min_row = (int)((next_y - CAR_COLLISION_RADIUS) / TILE_SIZE);
  int max_row = (int)((next_y + CAR_COLLISION_RADIUS) / TILE_SIZE);
  int row;
  int col;

  for (row = min_row; row <= max_row; row++) {
    for (col = min_col; col <= max_col; col++) {
      if (is_blocking_tile(col, row)) {
        return true;
      }
    }
  }

  return false;
}

bool check_player_police_collision(void) {
  float dx;
  float dy;
  float distance_sq;
  float min_distance;
  float distance;
  float nx;
  float ny;
  float overlap;

  if (!police_car.active) {
    return false;
  }

  dx = player.x - police_car.x;
  dy = player.y - police_car.y;
  min_distance = CAR_TO_CAR_RADIUS * 2.0f;
  distance_sq = dx * dx + dy * dy;

  if (distance_sq >= min_distance * min_distance) {
    return false;
  }

  distance = sqrtf(distance_sq);
  if (distance < 0.001f) {
    nx = 1.0f;
    ny = 0.0f;
    overlap = min_distance;
  } else {
    nx = dx / distance;
    ny = dy / distance;
    overlap = min_distance - distance;
  }

  player.x += nx * (overlap * 0.6f);
  player.y += ny * (overlap * 0.6f);
  police_car.x -= nx * (overlap * 0.6f);
  police_car.y -= ny * (overlap * 0.6f);

  player.speed *= -0.35f;
  police_car.speed *= -0.20f;

  if (check_collision(player.x, player.y)) {
    player.x -= nx * overlap;
    player.y -= ny * overlap;
    player.speed = 0.0f;
  }

  if (check_collision(police_car.x, police_car.y)) {
    police_car.x += nx * overlap;
    police_car.y += ny * overlap;
    police_car.speed = 0.0f;
  }

  dx = player.x - police_car.x;
  dy = player.y - police_car.y;
  distance_sq = dx * dx + dy * dy;
  if (distance_sq < min_distance * min_distance && distance_sq > 0.001f) {
    distance = sqrtf(distance_sq);
    nx = dx / distance;
    ny = dy / distance;
    overlap = min_distance - distance;
    player.x += nx * (overlap * 0.5f);
    player.y += ny * (overlap * 0.5f);
    police_car.x -= nx * (overlap * 0.5f);
    police_car.y -= ny * (overlap * 0.5f);
  }

  return true;
}

void update_player(void) {
  float old_x = player.x;
  float old_y = player.y;
  float old_speed = player.speed;
  float traction = (get_tile_at_world(player.x, player.y) == TILE_GRASS) ? grass_drag : road_drag;
  float dx;
  float dy;
  float next_x;
  float next_y;

  if (key_a) {
    player.angle += turn_rate * (0.4f + fabsf(player.speed));
  }

  if (key_d) {
    player.angle -= turn_rate * (0.4f + fabsf(player.speed));
  }

  if (player.angle > 2.0f * PI) {
    player.angle -= 2.0f * PI;
  } else if (player.angle < 0.0f) {
    player.angle += 2.0f * PI;
  }

  if (key_w) {
    player.speed += accel_forward;
  }

  if (key_s) {
    if (player.speed > 0.0f) {
      player.speed *= brake_drag;
      if (player.speed < 0.05f) {
        player.speed = 0.0f;
      }
    } else {
      player.speed -= accel_reverse;
    }
  }

  if (!key_w && !key_s) {
    player.speed *= idle_drag;
  }

  player.speed *= traction;
  player.speed = clamp_float(player.speed, max_reverse_speed, max_forward_speed);

  dx = cosf(player.angle) * player.speed;
  dy = -sinf(player.angle) * player.speed;
  next_x = player.x + dx;
  next_y = player.y + dy;

  if (!check_collision(next_x, player.y)) {
    player.x = next_x;
  } else {
    player.speed = old_speed * -0.2f;
  }

  if (!check_collision(player.x, next_y)) {
    player.y = next_y;
  } else {
    player.speed = old_speed * -0.2f;
  }

  if (check_collision(player.x, player.y)) {
    player.x = old_x;
    player.y = old_y;
    player.speed = 0.0f;
  }
}

void update_police_car(void) {
  float target_angle;
  float angle_diff;
  float next_x;
  float next_y;

  if (!police_car.active) {
    return;
  }

  target_angle = atan2f(-(player.y - police_car.y), player.x - police_car.x);
  angle_diff = target_angle - police_car.angle;

  while (angle_diff > PI) {
    angle_diff -= 2.0f * PI;
  }
  while (angle_diff < -PI) {
    angle_diff += 2.0f * PI;
  }

  if (angle_diff > police_turn_rate) {
    angle_diff = police_turn_rate;
  } else if (angle_diff < -police_turn_rate) {
    angle_diff = -police_turn_rate;
  }

  police_car.angle += angle_diff;

  if (police_car.angle > 2.0f * PI) {
    police_car.angle -= 2.0f * PI;
  } else if (police_car.angle < 0.0f) {
    police_car.angle += 2.0f * PI;
  }

  police_car.speed += police_accel;
  if (police_car.speed > police_max_speed) {
    police_car.speed = police_max_speed;
  }
  police_car.speed *= police_drag;

  next_x = police_car.x + cosf(police_car.angle) * police_car.speed;
  next_y = police_car.y - sinf(police_car.angle) * police_car.speed;

  if (!check_collision(next_x, police_car.y)) {
    police_car.x = next_x;
  } else {
    police_car.angle += PI * 0.35f;
    police_car.speed *= 0.5f;
  }

  if (!check_collision(police_car.x, next_y)) {
    police_car.y = next_y;
  } else {
    police_car.angle -= PI * 0.25f;
    police_car.speed *= 0.5f;
  }

  if (check_player_police_collision()) {
    police_car.angle = atan2f(-(player.y - police_car.y), player.x - police_car.x);
  }
}

void update_camera(void) {
  camera_x = player.x - (SCREEN_W / 2.0f);
  camera_y = player.y - (SCREEN_H / 2.0f);

  camera_x = clamp_float(camera_x, 0.0f, (float)(WORLD_W - SCREEN_W));
  camera_y = clamp_float(camera_y, 0.0f, (float)(WORLD_H - SCREEN_H));
}

void draw_world(void) {
  int start_col = (int)(camera_x / TILE_SIZE);
  int start_row = (int)(camera_y / TILE_SIZE);
  int end_col = (int)((camera_x + SCREEN_W) / TILE_SIZE) + 1;
  int end_row = (int)((camera_y + SCREEN_H) / TILE_SIZE) + 1;
  int row;
  int col;
  int screen_x;
  int screen_y;

  for (row = start_row; row <= end_row; row++) {
    for (col = start_col; col <= end_col; col++) {
      if (row < 0 || col < 0 || row >= MAP_ROWS || col >= MAP_COLS) {
        continue;
      }

      screen_x = world_to_screen_x((float)(col * TILE_SIZE));
      screen_y = world_to_screen_y((float)(row * TILE_SIZE));
      draw_tile(col, row, screen_x, screen_y);
    }
  }

  draw_police_car();
  draw_player();
}

void draw_tile(int col, int row, int screen_x, int screen_y) {
  TileType tile = world_map[row][col];
  short int fill = BLACK;

  if (tile == TILE_GRASS) {
    fill = DARK_GREEN;
  } else if (tile == TILE_ROAD) {
    fill = GRAY;
  } else if (tile == TILE_BUILDING) {
    fill = BROWN;
  } else if (tile == TILE_BORDER) {
    fill = BLUE;
  }

  draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, fill);

  if (tile == TILE_ROAD) {
    if ((row % 2) == 0) {
      draw_rect(screen_x + TILE_SIZE / 2 - 1, screen_y + 4, 2, TILE_SIZE - 8, YELLOW);
    } else {
      draw_rect(screen_x + 4, screen_y + TILE_SIZE / 2 - 1, TILE_SIZE - 8, 2, YELLOW);
    }
  } else if (tile == TILE_BUILDING) {
    draw_rect(screen_x + 3, screen_y + 3, TILE_SIZE - 6, TILE_SIZE - 6, RED);
    draw_rect(screen_x + 8, screen_y + 8, 5, 5, LIGHT_GRAY);
  } else if (tile == TILE_BORDER) {
    draw_rect(screen_x + 4, screen_y + 4, TILE_SIZE - 8, TILE_SIZE - 8, WHITE);
  } else {
    draw_rect(screen_x + 10, screen_y + 10, 4, 4, GREEN);
  }
}

void draw_player(void) {
  draw_filled_car(player.x, player.y, player.angle, WHITE, RED);
}

void draw_police_car(void) {
  if (!police_car.active) {
    return;
  }

  draw_filled_car(police_car.x, police_car.y, police_car.angle, BLUE, RED);
}

void draw_filled_car(float world_x, float world_y, float angle, short int body_color,
                     short int nose_color) {
  int cx = world_to_screen_x(world_x);
  int cy = world_to_screen_y(world_y);
  float forward_x = cosf(angle);
  float forward_y = -sinf(angle);
  float right_x = -forward_y;
  float right_y = forward_x;
  int half_length = (int)(CAR_LENGTH * 0.5f);
  int half_width = (int)(CAR_WIDTH * 0.5f);
  int along;
  int across;

  for (along = -half_length; along <= half_length; along++) {
    for (across = -half_width; across <= half_width; across++) {
      int px = cx + (int)(forward_x * (float)along + right_x * (float)across);
      int py = cy + (int)(forward_y * (float)along + right_y * (float)across);
      plot_pixel(px, py, body_color);
    }
  }

  draw_line(cx, cy, cx + (int)(forward_x * (CAR_LENGTH * 0.7f)),
            cy + (int)(forward_y * (CAR_LENGTH * 0.7f)), nose_color);
}

int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

float clamp_float(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

int world_to_screen_x(float world_x) {
  return (int)(world_x - camera_x);
}

int world_to_screen_y(float world_y) {
  return (int)(world_y - camera_y);
}
