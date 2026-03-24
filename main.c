#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PIXEL_BUF_CTRL_BASE 0xFF203020
#define PS2_BASE 0xFF200100

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

volatile int* pixel_ctrl_ptr = (int*)PIXEL_BUF_CTRL_BASE;
volatile int* PS2_ptr = (int*)PS2_BASE;

volatile int pixel_buffer_start;
short int Buffer1[240][512];
short int Buffer2[240][512];

static TileType world_map[MAP_ROWS][MAP_COLS];

static Player player;
static float camera_x = 0.0f;
static float camera_y = 0.0f;

static bool key_w = false;
static bool key_s = false;
static bool key_a = false;
static bool key_d = false;
static bool key_r = false;
static bool break_code = false;
static bool extended_code = false;

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

void init_buffers(void);
void wait_for_vsync(void);
void swap_buffers(void);

void plot_pixel(int x, int y, short int color);
void clear_screen(short int color);
void draw_rect(int x, int y, int width, int height, short int color);
void draw_line(int x0, int y0, int x1, int y1, short int color);

void seed_rng(unsigned int seed);
unsigned int rand_u32(void);
int rand_range(int low, int high);

void generate_map(void);
void add_road_row(int row);
void add_road_col(int col);
void place_buildings(void);

void reset_player(void);
void process_keyboard_ps2(void);
void handle_keyboard_byte(unsigned char byte);
void update_key_state(unsigned char scan, bool pressed);

bool is_blocking_tile(int col, int row);
TileType get_tile_at_world(float world_x, float world_y);
bool check_collision(float next_x, float next_y);

void update_player(void);
void update_camera(void);

void draw_world(void);
void draw_tile(int col, int row, int screen_x, int screen_y);
void draw_player(void);

int clamp_int(int value, int min_value, int max_value);
float clamp_float(float value, float min_value, float max_value);
int world_to_screen_x(float world_x);
int world_to_screen_y(float world_y);

int main(void) {
  init_buffers();
  seed_rng(0x24324324u);
  generate_map();
  reset_player();
  update_camera();

  while (1) {
    process_keyboard_ps2();

    if (key_r) {
      reset_player();
    } else {
      update_player();
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

void seed_rng(unsigned int seed) { rng_state = seed; }

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
}

bool is_blocking_tile(int col, int row) {
  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return true;
  }

  return world_map[row][col] == TILE_BUILDING ||
         world_map[row][col] == TILE_BORDER;
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

void update_player(void) {
  float old_x = player.x;
  float old_y = player.y;
  float old_speed = player.speed;
  float traction = (get_tile_at_world(player.x, player.y) == TILE_GRASS)
                       ? grass_drag
                       : road_drag;
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
  player.speed =
      clamp_float(player.speed, max_reverse_speed, max_forward_speed);

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
      draw_rect(screen_x + TILE_SIZE / 2 - 1, screen_y + 4, 2, TILE_SIZE - 8,
                YELLOW);
    } else {
      draw_rect(screen_x + 4, screen_y + TILE_SIZE / 2 - 1, TILE_SIZE - 8, 2,
                YELLOW);
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
  int cx = world_to_screen_x(player.x);
  int cy = world_to_screen_y(player.y);
  float forward_x = cosf(player.angle);
  float forward_y = -sinf(player.angle);
  float right_x = -forward_y;
  float right_y = forward_x;

  int nose_x = cx + (int)(forward_x * (CAR_LENGTH * 0.6f));
  int nose_y = cy + (int)(forward_y * (CAR_LENGTH * 0.6f));
  int tail_x = cx - (int)(forward_x * (CAR_LENGTH * 0.4f));
  int tail_y = cy - (int)(forward_y * (CAR_LENGTH * 0.4f));
  int left_x = cx + (int)(right_x * (CAR_WIDTH * 0.5f));
  int left_y = cy + (int)(right_y * (CAR_WIDTH * 0.5f));
  int right_xi = cx - (int)(right_x * (CAR_WIDTH * 0.5f));
  int right_yi = cy - (int)(right_y * (CAR_WIDTH * 0.5f));

  draw_line(left_x, left_y, nose_x, nose_y, WHITE);
  draw_line(right_xi, right_yi, nose_x, nose_y, WHITE);
  draw_line(left_x, left_y, tail_x, tail_y, WHITE);
  draw_line(right_xi, right_yi, tail_x, tail_y, WHITE);
  draw_line(tail_x, tail_y, nose_x, nose_y, RED);

  draw_rect(cx - 1, cy - 1, 3, 3, YELLOW);
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

int world_to_screen_x(float world_x) { return (int)(world_x - camera_x); }

int world_to_screen_y(float world_y) { return (int)(world_y - camera_y); }
