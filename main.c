#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PIXEL_BUF_CTRL_BASE 0xFF203020
#define PS2_BASE 0xFF200100
#define LEDR_BASE 0xFF200000
#define HEX3_HEX0_BASE 0xFF200020
#define HEX5_HEX4_BASE 0xFF200030

#define SCREEN_W 320
#define SCREEN_H 240

#define MAP_COLS 64
#define MAP_ROWS 48
#define TILE_SIZE 32
#define WORLD_W (MAP_COLS * TILE_SIZE)
#define WORLD_H (MAP_ROWS * TILE_SIZE)

#define CAR_WIDTH 20.0f
#define CAR_LENGTH 30.0f
#define CAR_COLLISION_RADIUS 9.0f
#define PLAYER_COLLISION_RADIUS 7.0f
#define CAR_TO_CAR_RADIUS 16.0f
#define POLICE_PLAYER_HIT_RADIUS 13.0f
#define CASH_PICKUP_RADIUS 18.0f

#define PI 3.14159265f
#define FRAME_RATE 60
#define MAX_LIVES 5
#define POLICE_FREEZE_FRAMES 70

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
#define DARK_GREEN 0x3666

typedef enum {
  TILE_GRASS = 0,
  TILE_ROAD = 1,
  TILE_BUILDING = 2,
  TILE_BORDER = 3
} TileType;

#define MAX_POLICE_CARS 4

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
volatile int* LEDR_ptr = (int*)LEDR_BASE;
volatile int* HEX3_HEX0_ptr = (int*)HEX3_HEX0_BASE;
volatile int* HEX5_HEX4_ptr = (int*)HEX5_HEX4_BASE;

volatile int pixel_buffer_start;
short int Buffer1[240][512];
short int Buffer2[240][512];

static TileType world_map[MAP_ROWS][MAP_COLS];
static bool cash_pickups[MAP_ROWS][MAP_COLS];
static int chase_distance_map[MAP_ROWS][MAP_COLS];
static int chase_map_player_col = -1;
static int chase_map_player_row = -1;

static Player player;
static PoliceCar police_cars[MAX_POLICE_CARS];
static float police_target_x[MAX_POLICE_CARS];
static float police_target_y[MAX_POLICE_CARS];
static float camera_x = 0.0f;
static float camera_y = 0.0f;
static int score = 0;
static int best_score = 0;
static int player_lives = MAX_LIVES;
static int police_freeze_frames = 0;

static bool key_w = false;
static bool key_s = false;
static bool key_a = false;
static bool key_d = false;
static bool key_r = false;
static bool break_code = false;
static bool extended_code = false;

static unsigned int rng_state = 0x12345678u;

static const float accel_forward = 0.36f;
static const float accel_reverse = 0.20f;
static const float grass_drag = 0.94f;
static const float road_drag = 0.985f;
static const float idle_drag = 0.985f;
static const float brake_drag = 0.88f;
static const float max_forward_speed = 6.6f;
static const float max_reverse_speed = -3.0f;
static const float turn_rate = 0.042f;
static const float police_accel = 0.30f;
static const float police_drag = 0.97f;
static const float police_max_speed = 6.8f;
static const float police_turn_rate = 0.090f;

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
void widen_tight_grass_paths(void);
void ensure_spawn_area(void);
void reset_cash_pickups(void);
void collect_cash_pickups(void);
void update_score_display(void);
void update_lives_display(void);
unsigned char encode_hex_digit(int digit);
bool pickup_can_spawn_at(int col, int row);

void reset_player(void);
void reset_player_status(void);
void spawn_police_car(void);
void spawn_police_car_at_index(int index);
int get_target_police_count(void);
void sync_police_cars_to_score(void);
void process_keyboard_ps2(void);
void handle_keyboard_byte(unsigned char byte);
void update_key_state(unsigned char scan, bool pressed);

bool is_blocking_tile(int col, int row);
TileType get_tile_at_world(float world_x, float world_y);
bool check_collision(float next_x, float next_y);
bool check_player_collision(float next_x, float next_y);
bool is_road_tile(int col, int row);
bool has_line_of_sight(float start_x, float start_y, float end_x, float end_y);
void update_chase_distance_map(void);
void reset_police_targets(void);
void ensure_police_car_valid(int index);
bool check_player_police_collision(void);
bool check_player_police_collision_for_index(int index);
void register_player_hit(void);
void register_player_hit_for_index(int index);
void update_timers(void);
void get_police_difficulty(float* accel, float* drag, float* max_speed,
                           float* turn_rate, float* lead_scale);

void update_player(void);
void update_single_police_car(int index);
void update_police_car(void);
void update_camera(void);

void draw_world(void);
void draw_game_over_screen(void);
void draw_text(int x, int y, const char* text, int scale, short int color);
void draw_number(int x, int y, int value, int scale, short int color);
int get_text_width(const char* text, int scale);
unsigned char get_glyph_row(char ch, int row);
void draw_health_bar(void);
void draw_tile(int col, int row, int screen_x, int screen_y);
void draw_player(void);
void draw_single_police_car(int index);
void draw_police_car(void);
void draw_filled_car(float world_x, float world_y, float angle,
                     short int body_color, short int nose_color);

int clamp_int(int value, int min_value, int max_value);
float clamp_float(float value, float min_value, float max_value);
int world_to_screen_x(float world_x);
int world_to_screen_y(float world_y);

// Main game loop
int main(void) {
  init_buffers();
  seed_rng(0x24324324u);
  generate_map();
  reset_cash_pickups();
  reset_player();
  reset_player_status();
  spawn_police_car();
  update_camera();
  update_score_display();

  // Game loop
  while (1) {
    process_keyboard_ps2();

    if (key_r) {
      reset_cash_pickups();
      reset_player();
      reset_player_status();
      spawn_police_car();
    } else if (player_lives > 0) {
      update_timers();
      update_player();
      collect_cash_pickups();
      update_police_car();
      check_player_police_collision();
    }

    update_camera();
    if (player_lives > 0) {
      draw_world();
    } else {
      draw_game_over_screen();
    }
    swap_buffers();
  }
  return 0;
}

// Declare double buffering functions and game logic functions
void init_buffers(void) {
  *(pixel_ctrl_ptr + 1) = (int)&Buffer1;
  wait_for_vsync();
  pixel_buffer_start = *pixel_ctrl_ptr;
  clear_screen(BLACK);

  *(pixel_ctrl_ptr + 1) = (int)&Buffer2;
  pixel_buffer_start = *(pixel_ctrl_ptr + 1);
  clear_screen(BLACK);
}

// Wait for vsync for buffer swap and prevent tear
void wait_for_vsync(void) {
  *pixel_ctrl_ptr = 1;
  while ((*(pixel_ctrl_ptr + 3) & 0x01) != 0) {
  }
}

// Swap the front and back buffers
void swap_buffers(void) {
  wait_for_vsync();
  pixel_buffer_start = *(pixel_ctrl_ptr + 1);
}

// Plot a pixel at (x, y) with the given color
void plot_pixel(int x, int y, short int color) {
  volatile short int* pixel_addr;

  // Check bounds
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
    return;
  }

  pixel_addr = (volatile short int*)(pixel_buffer_start + (y << 10) + (x << 1));
  *pixel_addr = color;
}

// Fill the entire screen with a single color
void clear_screen(short int color) {
  int y;
  int x;

  for (y = 0; y < SCREEN_H; y++) {
    volatile short int* row_addr =
        (volatile short int*)(pixel_buffer_start + (y << 10));
    for (x = 0; x < SCREEN_W; x++) {
      row_addr[x] = color;
    }
  }
}

// Draw a filled rectangle at (x, y) with the specified width, height, and color
void draw_rect(int x, int y, int width, int height, short int color) {
  int start_x = clamp_int(x, 0, SCREEN_W);
  int start_y = clamp_int(y, 0, SCREEN_H);
  int end_x = clamp_int(x + width, 0, SCREEN_W);
  int end_y = clamp_int(y + height, 0, SCREEN_H);
  int row;

  for (row = start_y; row < end_y; row++) {
    volatile short int* row_addr =
        (volatile short int*)(pixel_buffer_start + (row << 10));
    int col;
    for (col = start_x; col < end_x; col++) {
      row_addr[col] = color;
    }
  }
}

// Bresenham's line drawing algorithm
void draw_line(int x0, int y0, int x1, int y1, short int color) {
  int steep = (abs(y1 - y0) > abs(x1 - x0));
  int temp;
  int dx;
  int dy;
  int error;
  int y_step;
  int x;

  // If the line is steep, swap x and y coordinates
  if (steep) {
    temp = x0;
    x0 = y0;
    y0 = temp;
    temp = x1;
    x1 = y1;
    y1 = temp;
  }

  // Ensure that we are drawing from left to right
  if (x0 > x1) {
    temp = x0;
    x0 = x1;
    x1 = temp;
    temp = y0;
    y0 = y1;
    y1 = temp;
  }

  // Compute differences and error term
  dx = x1 - x0;
  dy = abs(y1 - y0);
  error = -(dx / 2);
  y_step = (y0 < y1) ? 1 : -1;

  // Loop through x coordinates and plot the line
  for (x = x0; x <= x1; x++) {
    if (steep) {
      plot_pixel(y0, x, color);
    } else {
      plot_pixel(x, y0, color);
    }

    // Update error term and y coordinate if necessary
    error += dy;
    if (error >= 0) {
      y0 += y_step;
      error -= dx;
    }
  }
}

// Random number generator
void seed_rng(unsigned int seed) { rng_state = seed; }

// Generates a random unsigned 32-bit integer using a linear congruential
// generator algorithm
unsigned int rand_u32(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state;
}

// Generates a random integer in the range [low, high] using the rand_u32
// function
int rand_range(int low, int high) {
  unsigned int span = (unsigned int)(high - low + 1);
  return low + (int)(rand_u32() % span);
}

// Generate the world map with roads, buildings, and cash pickups
void generate_map(void) {
  int row;
  int col;
  int road_rows[12];
  int road_cols[12];
  int row_count;
  int col_count;
  int next_row;
  int next_col;
  int corridor;
  int start;
  int end;
  int branch;
  int temp;
  int branch_row;
  int branch_col;
  int length;
  int dir;
  int carve_col;
  int carve_row;

  row_count = 0;
  col_count = 0;

  // Initialize the world map with grass and borders
  for (row = 0; row < MAP_ROWS; row++) {
    for (col = 0; col < MAP_COLS; col++) {
      if (row == 0 || col == 0 || row == MAP_ROWS - 1 || col == MAP_COLS - 1) {
        world_map[row][col] = TILE_BORDER;
      } else {
        world_map[row][col] = TILE_GRASS;
      }
    }
  }

  // Add initial roads and then randomly generate additional
  road_rows[row_count++] = 2;
  road_cols[col_count++] = 2;

  // Generate horizontal roads
  next_row = 7 + rand_range(0, 2);
  while (next_row < MAP_ROWS - 4 && row_count < 9) {
    road_rows[row_count++] = next_row;
    next_row += rand_range(6, 8);
  }

  // Generate vertical roads
  next_col = 7 + rand_range(0, 2);
  while (next_col < MAP_COLS - 4 && col_count < 9) {
    road_cols[col_count++] = next_col;
    next_col += rand_range(6, 8);
  }

  // Add a main road roughly through the center of the map
  road_rows[row_count++] =
      clamp_int(MAP_ROWS / 2 + rand_range(-2, 2), 2, MAP_ROWS - 3);
  road_cols[col_count++] =
      clamp_int(MAP_COLS / 2 + rand_range(-2, 2), 2, MAP_COLS - 3);

  // Add horizontal roads to map based on generated road row/column positions,
  // with some random variation
  for (row = 0; row < row_count; row++) {
    add_road_row(road_rows[row]);
    if ((rand_u32() & 3u) == 0u) {
      add_road_row(road_rows[row] + 1);
    }
  }

  // Add vertical roads to map based on generated road row/column positions,
  // with some random variation
  for (col = 0; col < col_count; col++) {
    add_road_col(road_cols[col]);
    if ((rand_u32() & 3u) == 0u) {
      add_road_col(road_cols[col] + 1);
    }
  }

  // Add additonal random corridors between generated roads
  for (row = 0; row < row_count - 1; row++) {
    corridor = rand_range(road_rows[row], road_rows[row + 1]);
    start = road_cols[rand_range(0, col_count - 1)];
    end = road_cols[rand_range(0, col_count - 1)];
    if (start > end) {
      temp = start;
      start = end;
      end = temp;
    }

    // Clamp the corridor so it doesnt go out of bounds
    corridor = clamp_int(corridor, 1, MAP_ROWS - 2);
    for (col = start; col <= end; col++) {
      world_map[corridor][col] = TILE_ROAD;
      if ((rand_u32() & 1u) == 0u && corridor + 1 < MAP_ROWS - 1) {
        world_map[corridor + 1][col] = TILE_ROAD;
      }
    }
  }

  // Add additonal random corridors between generated roads
  for (col = 0; col < col_count - 1; col++) {
    corridor = rand_range(road_cols[col], road_cols[col + 1]);
    start = road_rows[rand_range(0, row_count - 1)];
    end = road_rows[rand_range(0, row_count - 1)];
    if (start > end) {
      temp = start;
      start = end;
      end = temp;
    }

    corridor = clamp_int(corridor, 1, MAP_COLS - 2);
    for (row = start; row <= end; row++) {
      world_map[row][corridor] = TILE_ROAD;
      if ((rand_u32() & 1u) == 0u && corridor + 1 < MAP_COLS - 1) {
        world_map[row][corridor + 1] = TILE_ROAD;
      }
    }
  }

  // Add random branches off the main roads
  for (branch = 0; branch < 14; branch++) {
    branch_row = rand_range(2, MAP_ROWS - 3);
    branch_col = rand_range(2, MAP_COLS - 3);
    length = rand_range(2, 5);
    dir = ((rand_u32() & 1u) == 0u) ? -1 : 1;

    if (!is_road_tile(branch_col, branch_row)) {
      continue;
    }

    if ((rand_u32() & 1u) == 0u) {
      for (col = 0; col < length; col++) {
        carve_col = branch_col + dir * col;
        if (carve_col <= 0 || carve_col >= MAP_COLS - 1) {
          break;
        }
        world_map[branch_row][carve_col] = TILE_ROAD;
      }
    } else {
      for (row = 0; row < length; row++) {
        carve_row = branch_row + dir * row;
        if (carve_row <= 0 || carve_row >= MAP_ROWS - 1) {
          break;
        }
        world_map[carve_row][branch_col] = TILE_ROAD;
      }
    }
  }

  place_buildings();
  widen_tight_grass_paths();
  ensure_spawn_area();
}

// Add a horizontal road at the specified row, with bounds checking
void add_road_row(int row) {
  int col;

  row = clamp_int(row, 1, MAP_ROWS - 2);
  for (col = 1; col < MAP_COLS - 1; col++) {
    world_map[row][col] = TILE_ROAD;
  }
}

// Add a vertical road at the specified column, with bounds checking
void add_road_col(int col) {
  int row;

  col = clamp_int(col, 1, MAP_COLS - 2);
  for (row = 1; row < MAP_ROWS - 1; row++) {
    world_map[row][col] = TILE_ROAD;
  }
}

// Place buildings on the map in random locations
void place_buildings(void) {
  int row;
  int col;
  int block_h;
  int block_w;
  int r;
  int c;
  int check_row;
  int check_col;
  bool can_place;

  // Randomly place a building block on the map of two possible sizes
  // Ensure no overlapping with roads or other buildings, and leave a gap of at
  // least 2 tiles
  for (row = 2; row < MAP_ROWS - 3; row++) {
    for (col = 2; col < MAP_COLS - 3; col++) {
      if (world_map[row][col] != TILE_GRASS) {
        continue;
      }

      if ((rand_u32() % 100u) >= 48u) {
        continue;
      }

      block_h = rand_range(1, 2);
      block_w = rand_range(1, 2);
      can_place = true;

      // Check a 2 tile border around the proposed building location to ensure
      // there is enough space and no roads nearby
      for (check_row = row - 2; check_row <= row + block_h + 1 && can_place;
           check_row++) {
        for (check_col = col - 2; check_col <= col + block_w + 1; check_col++) {
          if (check_row <= 0 || check_col <= 0 || check_row >= MAP_ROWS - 1 ||
              check_col >= MAP_COLS - 1) {
            can_place = false;
            break;
          }

          if (world_map[check_row][check_col] == TILE_BUILDING) {
            can_place = false;
            break;
          }
        }
      }

      // Cannot place building if no enough space or too close to another
      // building or road
      if (!can_place) {
        continue;
      }

      // Check the planned building area to ensure only grass and no overlap
      // with roads or other buildings
      for (r = row; r < row + block_h; r++) {
        for (c = col; c < col + block_w; c++) {
          if (world_map[r][c] != TILE_GRASS) {
            can_place = false;
          }
        }
      }

      // Cannot place building if planned area is not clear grass
      if (!can_place) {
        continue;
      }

      // Place the building by marking the tiles as TILE_BUILDING
      for (r = row; r < row + block_h; r++) {
        for (c = col; c < col + block_w; c++) {
          world_map[r][c] = TILE_BUILDING;
        }
      }
    }
  }
}

// Prevent one-tile wide grass paths between roads from being too narrow
// Adds extra grass tiles to widen path if only one tile between two buildings
void widen_tight_grass_paths(void) {
  int pass;
  int row;
  int col;
  bool left_blocked;
  bool right_blocked;
  bool up_blocked;
  bool down_blocked;
  bool vertical_corridor;
  bool horizontal_corridor;
  bool touches_road;

  for (pass = 0; pass < 2; pass++) {
    for (row = 1; row < MAP_ROWS - 1; row++) {
      for (col = 1; col < MAP_COLS - 1; col++) {
        if (world_map[row][col] != TILE_GRASS) {
          continue;
        }

        left_blocked = is_blocking_tile(col - 1, row);
        right_blocked = is_blocking_tile(col + 1, row);
        up_blocked = is_blocking_tile(col, row - 1);
        down_blocked = is_blocking_tile(col, row + 1);
        vertical_corridor = !up_blocked && !down_blocked;
        horizontal_corridor = !left_blocked && !right_blocked;
        touches_road = is_road_tile(col - 1, row) ||
                       is_road_tile(col + 1, row) ||
                       is_road_tile(col, row - 1) || is_road_tile(col, row + 1);

        if (touches_road && left_blocked && right_blocked &&
            vertical_corridor) {
          if (world_map[row][col - 1] == TILE_BUILDING) {
            world_map[row][col - 1] = TILE_GRASS;
          }
          if (world_map[row][col + 1] == TILE_BUILDING) {
            world_map[row][col + 1] = TILE_GRASS;
          }
        }

        if (touches_road && up_blocked && down_blocked && horizontal_corridor) {
          if (world_map[row - 1][col] == TILE_BUILDING) {
            world_map[row - 1][col] = TILE_GRASS;
          }
          if (world_map[row + 1][col] == TILE_BUILDING) {
            world_map[row + 1][col] = TILE_GRASS;
          }
        }
      }
    }
  }
}

// Ensure spawn area is clear
void ensure_spawn_area(void) {
  int row;
  int col;

  // Clear a spawn area that is empty and easy for user to navigate
  for (row = 1; row < 5; row++) {
    for (col = 1; col < 5; col++) {
      world_map[row][col] = TILE_ROAD;
    }
  }

  for (row = 1; row < 7; row++) {
    world_map[row][2] = TILE_ROAD;
    world_map[row][3] = TILE_ROAD;
  }

  for (col = 1; col < 8; col++) {
    world_map[2][col] = TILE_ROAD;
    world_map[3][col] = TILE_ROAD;
  }
}

// Reset cash pickups on the map - clear and replace
void reset_cash_pickups(void) {
  int row;
  int col;

  score = 0;

  // Clear all cash pickups from the map
  for (row = 0; row < MAP_ROWS; row++) {
    for (col = 0; col < MAP_COLS; col++) {
      cash_pickups[row][col] = false;
    }
  }

  // Random place cash pickups on map
  for (row = 0; row < MAP_ROWS; row++) {
    for (col = 0; col < MAP_COLS; col++) {
      if (pickup_can_spawn_at(col, row) && (rand_u32() % 100u) < 22u) {
        cash_pickups[row][col] = true;
      }
    }
  }

  cash_pickups[1][1] = false;
  update_score_display();
}

// Check if cash can spawn at specified location
bool pickup_can_spawn_at(int col, int row) {
  int check_row;
  int check_col;

  if (!is_road_tile(col, row)) {
    return false;
  }

  if (row <= 3 && col <= 3) {
    return false;
  }

  for (check_row = row - 1; check_row <= row + 1; check_row++) {
    for (check_col = col - 1; check_col <= col + 1; check_col++) {
      if (check_row < 0 || check_col < 0 || check_row >= MAP_ROWS ||
          check_col >= MAP_COLS) {
        continue;
      }

      if (cash_pickups[check_row][check_col]) {
        return false;
      }
    }
  }

  return true;
}

// Reset player position, angle, and speed to initial values
void reset_player(void) {
  player.x = 1.5f * TILE_SIZE;
  player.y = 1.5f * TILE_SIZE;
  player.angle = 0.0f;
  player.speed = 0.0f;
}

// Reset player lives and any active police freeze timer
void reset_player_status(void) {
  player_lives = MAX_LIVES;
  police_freeze_frames = 0;
  reset_police_targets();
  update_lives_display();
}

// Spawn police car at random road location, set inital direction towards player
int get_target_police_count(void) {
  int count = 1 + score / 5;

  if (count > MAX_POLICE_CARS) {
    count = MAX_POLICE_CARS;
  }

  return count;
}

void spawn_police_car_at_index(int index) {
  int attempts;
  int col;
  int row;
  float spawn_x;
  float spawn_y;
  float dx;
  float dy;

  police_cars[index].active = false;
  police_cars[index].speed = 0.0f;
  police_cars[index].angle = PI / 2.0f;
  police_target_x[index] = player.x;
  police_target_y[index] = player.y;

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

    if (dx * dx + dy * dy < 170.0f * 170.0f) {
      continue;
    }

    police_cars[index].x = spawn_x;
    police_cars[index].y = spawn_y;
    police_cars[index].angle = atan2f(-(player.y - police_cars[index].y),
                                      player.x - police_cars[index].x);
    police_cars[index].speed = 0.0f;
    police_cars[index].active = true;
    police_target_x[index] = player.x;
    police_target_y[index] = player.y;
    return;
  }

  police_cars[index].x = (MAP_COLS - 3.0f) * TILE_SIZE;
  police_cars[index].y = (MAP_ROWS - 3.0f) * TILE_SIZE;
  police_cars[index].angle = PI;
  police_cars[index].speed = 0.0f;
  police_cars[index].active = true;
  police_target_x[index] = player.x;
  police_target_y[index] = player.y;
}

void spawn_police_car(void) {
  int i;
  int target_count = get_target_police_count();

  for (i = 0; i < MAX_POLICE_CARS; i++) {
    police_cars[i].active = false;
    police_cars[i].speed = 0.0f;
    police_cars[i].angle = PI / 2.0f;
  }

  for (i = 0; i < target_count; i++) {
    spawn_police_car_at_index(i);
  }
}

void sync_police_cars_to_score(void) {
  int i;
  int target_count = get_target_police_count();

  for (i = 0; i < target_count; i++) {
    if (!police_cars[i].active) {
      spawn_police_car_at_index(i);
    }
  }

  for (i = target_count; i < MAX_POLICE_CARS; i++) {
    police_cars[i].active = false;
    police_cars[i].speed = 0.0f;
  }
}

// Read PS/2 keyboard input and update key states accordingly
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

// Handle a single byte from the PS/2 keyboard, updating key states for pressed
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

// Update the state of the keys based on the scan code and whether it was a
// press or release
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

// Check if the tile at the specified column and row is blocking (building or
// border)
bool is_blocking_tile(int col, int row) {
  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return true;
  }

  return world_map[row][col] == TILE_BUILDING ||
         world_map[row][col] == TILE_BORDER;
}

// Check if the tile at the specified column and row is a road tile
bool is_road_tile(int col, int row) {
  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return false;
  }

  return world_map[row][col] == TILE_ROAD;
}

// Get the type of tile at the specified world coordinates, with bounds checking
TileType get_tile_at_world(float world_x, float world_y) {
  int col = (int)(world_x / TILE_SIZE);
  int row = (int)(world_y / TILE_SIZE);

  if (col < 0 || row < 0 || col >= MAP_COLS || row >= MAP_ROWS) {
    return TILE_BORDER;
  }

  return world_map[row][col];
}

// Check if a collision would occur at the specified world coordinates by
// checking tiles around car collision radius
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

bool check_player_collision(float next_x, float next_y) {
  int min_col = (int)((next_x - PLAYER_COLLISION_RADIUS) / TILE_SIZE);
  int max_col = (int)((next_x + PLAYER_COLLISION_RADIUS) / TILE_SIZE);
  int min_row = (int)((next_y - PLAYER_COLLISION_RADIUS) / TILE_SIZE);
  int max_row = (int)((next_y + PLAYER_COLLISION_RADIUS) / TILE_SIZE);
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

bool has_line_of_sight(float start_x, float start_y, float end_x, float end_y) {
  float dx = end_x - start_x;
  float dy = end_y - start_y;
  float distance = sqrtf(dx * dx + dy * dy);
  int steps;
  int step;

  if (distance < 1.0f) {
    return true;
  }

  steps = (int)(distance / 8.0f);
  if (steps < 1) {
    steps = 1;
  }

  for (step = 1; step < steps; step++) {
    float t = (float)step / (float)steps;
    float sample_x = start_x + dx * t;
    float sample_y = start_y + dy * t;

    if (is_blocking_tile((int)(sample_x / TILE_SIZE),
                         (int)(sample_y / TILE_SIZE))) {
      return false;
    }
  }

  return true;
}

void update_chase_distance_map(void) {
  const int max_tiles = MAP_ROWS * MAP_COLS;
  int queue_cols[MAP_ROWS * MAP_COLS];
  int queue_rows[MAP_ROWS * MAP_COLS];
  int head = 0;
  int tail = 0;
  int start_col = clamp_int((int)(player.x / TILE_SIZE), 0, MAP_COLS - 1);
  int start_row = clamp_int((int)(player.y / TILE_SIZE), 0, MAP_ROWS - 1);
  int row;
  int col;

  if (start_col == chase_map_player_col && start_row == chase_map_player_row) {
    return;
  }

  chase_map_player_col = start_col;
  chase_map_player_row = start_row;

  for (row = 0; row < MAP_ROWS; row++) {
    for (col = 0; col < MAP_COLS; col++) {
      chase_distance_map[row][col] = -1;
    }
  }

  if (is_blocking_tile(start_col, start_row)) {
    return;
  }

  chase_distance_map[start_row][start_col] = 0;
  queue_cols[tail] = start_col;
  queue_rows[tail] = start_row;
  tail++;

  while (head < tail && tail <= max_tiles) {
    static const int dcol[4] = {1, -1, 0, 0};
    static const int drow[4] = {0, 0, 1, -1};
    int current_col = queue_cols[head];
    int current_row = queue_rows[head];
    int current_distance = chase_distance_map[current_row][current_col];
    int dir;

    head++;

    for (dir = 0; dir < 4; dir++) {
      int next_col = current_col + dcol[dir];
      int next_row = current_row + drow[dir];

      if (next_col < 0 || next_row < 0 || next_col >= MAP_COLS ||
          next_row >= MAP_ROWS) {
        continue;
      }

      if (is_blocking_tile(next_col, next_row) ||
          chase_distance_map[next_row][next_col] >= 0) {
        continue;
      }

      chase_distance_map[next_row][next_col] = current_distance + 1;
      queue_cols[tail] = next_col;
      queue_rows[tail] = next_row;
      tail++;
    }
  }
}

void reset_police_targets(void) {
  int i;

  for (i = 0; i < MAX_POLICE_CARS; i++) {
    police_target_x[i] = player.x;
    police_target_y[i] = player.y;
  }
}

void ensure_police_car_valid(int index) {
  if (!police_cars[index].active) {
    return;
  }

  if (check_collision(police_cars[index].x, police_cars[index].y)) {
    spawn_police_car_at_index(index);
  }
}

// Check for collision between player and police car, and resolve by pushing
// them apart and reducing speed
bool check_player_police_collision_for_index(int index) {
  float dx;
  float dy;
  float distance_sq;
  float min_distance;
  float distance;
  float nx;
  float ny;
  float overlap;
  PoliceCar* police_car = &police_cars[index];

  if (!police_car->active) {
    return false;
  }

  dx = player.x - police_car->x;
  dy = player.y - police_car->y;
  min_distance = POLICE_PLAYER_HIT_RADIUS * 2.0f;
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
  police_car->x -= nx * (overlap * 0.6f);
  police_car->y -= ny * (overlap * 0.6f);

  player.speed *= -0.35f;
  police_car->speed *= -0.20f;

  if (check_collision(player.x, player.y)) {
    player.x -= nx * overlap;
    player.y -= ny * overlap;
    player.speed = 0.0f;
  }

  if (check_collision(police_car->x, police_car->y)) {
    police_car->x += nx * overlap;
    police_car->y += ny * overlap;
    police_car->speed = 0.0f;
  }

  dx = player.x - police_car->x;
  dy = player.y - police_car->y;
  distance_sq = dx * dx + dy * dy;
  if (distance_sq < min_distance * min_distance && distance_sq > 0.001f) {
    distance = sqrtf(distance_sq);
    nx = dx / distance;
    ny = dy / distance;
    overlap = min_distance - distance;
    player.x += nx * (overlap * 0.5f);
    player.y += ny * (overlap * 0.5f);
    police_car->x -= nx * (overlap * 0.5f);
    police_car->y -= ny * (overlap * 0.5f);
  }

  if (police_freeze_frames <= 0) {
    register_player_hit_for_index(index);
  }

  return true;
}

bool check_player_police_collision(void) {
  int i;
  bool collided = false;

  for (i = 0; i < MAX_POLICE_CARS; i++) {
    if (check_player_police_collision_for_index(i)) {
      collided = true;
    }
  }

  return collided;
}

// Take one life on contact, then freeze police for a short grace period
void register_player_hit_for_index(int index) {
  float dx;
  float dy;
  float distance;
  float nx;
  float ny;
  float candidate_x;
  float candidate_y;
  float safe_distance;
  float side_x;
  float side_y;
  int attempt;
  int side;
  bool found_spot = false;
  PoliceCar* police_car = &police_cars[index];

  if (police_freeze_frames > 0 || player_lives <= 0) {
    return;
  }

  player_lives--;
  player.speed = 0.0f;
  police_car->speed = 0.0f;

  dx = police_car->x - player.x;
  dy = police_car->y - player.y;
  distance = sqrtf(dx * dx + dy * dy);
  if (distance < 0.001f) {
    nx = 1.0f;
    ny = 0.0f;
  } else {
    nx = dx / distance;
    ny = dy / distance;
  }

  police_car->angle =
      atan2f(-(player.y - police_car->y), player.x - police_car->x);
  police_target_x[index] = player.x;
  police_target_y[index] = player.y;
  police_freeze_frames = POLICE_FREEZE_FRAMES;

  if (check_collision(police_car->x, police_car->y)) {
    side_x = -ny;
    side_y = nx;

    for (attempt = 2; attempt <= 5 && !found_spot; attempt++) {
      safe_distance = POLICE_PLAYER_HIT_RADIUS * (float)attempt;

      for (side = -1; side <= 1; side++) {
        candidate_x = police_car->x + nx * safe_distance +
                      side_x * (float)side * POLICE_PLAYER_HIT_RADIUS;
        candidate_y = police_car->y + ny * safe_distance +
                      side_y * (float)side * POLICE_PLAYER_HIT_RADIUS;

        if (!check_collision(candidate_x, candidate_y)) {
          police_car->x = candidate_x;
          police_car->y = candidate_y;
          found_spot = true;
          break;
        }
      }
    }

    if (!found_spot) {
      spawn_police_car_at_index(index);
    }
  }

  update_lives_display();
}

void register_player_hit(void) { register_player_hit_for_index(0); }

// Count down frame-based timers once per game loop
void update_timers(void) {
  if (police_freeze_frames > 0) {
    police_freeze_frames--;
    if (police_freeze_frames == 0) {
      int i;
      for (i = 0; i < MAX_POLICE_CARS; i++) {
        if (police_cars[i].active) {
          police_cars[i].speed = 1.2f;
        }
      }
    }
  }
}

// Increase police chase strength as the score grows
void get_police_difficulty(float* accel, float* drag, float* max_speed,
                           float* turn_rate, float* lead_scale) {
  float difficulty = clamp_float((float)score / 20.0f, 0.0f, 1.0f);

  *accel = police_accel + 0.18f * difficulty;
  *drag = police_drag + 0.015f * difficulty;
  *max_speed = police_max_speed + 2.5f * difficulty;
  *turn_rate = police_turn_rate + 0.050f * difficulty;
  *lead_scale = 8.0f + 16.0f * difficulty;
}

// Check for cash pickups within pickup radius of player, collect them, and
// update score
void collect_cash_pickups(void) {
  int min_col = clamp_int((int)((player.x - CASH_PICKUP_RADIUS) / TILE_SIZE), 0,
                          MAP_COLS - 1);
  int max_col = clamp_int((int)((player.x + CASH_PICKUP_RADIUS) / TILE_SIZE), 0,
                          MAP_COLS - 1);
  int min_row = clamp_int((int)((player.y - CASH_PICKUP_RADIUS) / TILE_SIZE), 0,
                          MAP_ROWS - 1);
  int max_row = clamp_int((int)((player.y + CASH_PICKUP_RADIUS) / TILE_SIZE), 0,
                          MAP_ROWS - 1);
  int row;
  int col;
  bool score_changed = false;

  for (row = min_row; row <= max_row; row++) {
    for (col = min_col; col <= max_col; col++) {
      float pickup_x;
      float pickup_y;
      float dx;
      float dy;

      if (!cash_pickups[row][col]) {
        continue;
      }

      pickup_x = ((float)col + 0.5f) * TILE_SIZE;
      pickup_y = ((float)row + 0.5f) * TILE_SIZE;
      dx = player.x - pickup_x;
      dy = player.y - pickup_y;

      if (dx * dx + dy * dy <= CASH_PICKUP_RADIUS * CASH_PICKUP_RADIUS) {
        cash_pickups[row][col] = false;
        score++;
        score_changed = true;
      }
    }
  }

  if (score_changed) {
    if (score > best_score) {
      best_score = score;
    }
    update_score_display();
  }
}

// Update the player's position, angle, and speed based on input and collisions
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

  if (!check_player_collision(next_x, player.y)) {
    player.x = next_x;
  } else {
    player.speed = old_speed * -0.2f;
  }

  if (!check_player_collision(player.x, next_y)) {
    player.y = next_y;
  } else {
    player.speed = old_speed * -0.2f;
  }

  if (check_player_collision(player.x, player.y)) {
    player.x = old_x;
    player.y = old_y;
    player.speed = 0.0f;
  }
}

// Update the police car's position, angle, and speed to chase the player, with
// simple steering and collision avoiding
void update_single_police_car(int index) {
  float chase_accel;
  float chase_drag;
  float chase_max_speed;
  float chase_turn_rate;
  float lead_scale;
  float target_x;
  float target_y;
  float target_angle;
  float angle_diff;
  float next_x;
  float next_y;
  int police_col;
  int police_row;
  int best_col;
  int best_row;
  int best_distance;
  int step_col;
  int step_row;
  PoliceCar* police_car = &police_cars[index];

  if (!police_car->active) {
    return;
  }

  if (police_freeze_frames > 0) {
    police_car->speed = 0.0f;
    return;
  }

  get_police_difficulty(&chase_accel, &chase_drag, &chase_max_speed,
                        &chase_turn_rate, &lead_scale);

  police_target_x[index] = player.x;
  police_target_y[index] = player.y;

  if (has_line_of_sight(police_car->x, police_car->y, player.x, player.y)) {
    police_target_x[index] =
        player.x + cosf(player.angle) * player.speed * lead_scale;
    police_target_y[index] =
        player.y - sinf(player.angle) * player.speed * lead_scale;
  } else {
    police_col = clamp_int((int)(police_car->x / TILE_SIZE), 0, MAP_COLS - 1);
    police_row = clamp_int((int)(police_car->y / TILE_SIZE), 0, MAP_ROWS - 1);
    best_col = police_col;
    best_row = police_row;
    best_distance = chase_distance_map[police_row][police_col];

    for (step_row = -1; step_row <= 1; step_row++) {
      for (step_col = -1; step_col <= 1; step_col++) {
        int next_col = police_col + step_col;
        int next_row = police_row + step_row;
        int next_distance;

        if ((step_col == 0 && step_row == 0) || (step_col != 0 && step_row != 0)) {
          continue;
        }

        if (next_col < 0 || next_row < 0 || next_col >= MAP_COLS ||
            next_row >= MAP_ROWS || is_blocking_tile(next_col, next_row)) {
          continue;
        }

        next_distance = chase_distance_map[next_row][next_col];
        if (next_distance < 0) {
          continue;
        }

        if (best_distance < 0 || next_distance < best_distance) {
          best_distance = next_distance;
          best_col = next_col;
          best_row = next_row;
        }
      }
    }

    if (best_distance >= 0) {
      police_target_x[index] = ((float)best_col + 0.5f) * TILE_SIZE;
      police_target_y[index] = ((float)best_row + 0.5f) * TILE_SIZE;
    }
  }

  target_x = police_target_x[index];
  target_y = police_target_y[index];
  target_x =
      clamp_float(target_x, 1.5f * TILE_SIZE, (MAP_COLS - 1.5f) * TILE_SIZE);
  target_y =
      clamp_float(target_y, 1.5f * TILE_SIZE, (MAP_ROWS - 1.5f) * TILE_SIZE);

  target_angle = atan2f(-(target_y - police_car->y), target_x - police_car->x);
  angle_diff = target_angle - police_car->angle;

  while (angle_diff > PI) {
    angle_diff -= 2.0f * PI;
  }
  while (angle_diff < -PI) {
    angle_diff += 2.0f * PI;
  }

  chase_turn_rate *= 1.0f + 0.55f * clamp_float(fabsf(angle_diff) / PI, 0.0f, 1.0f);

  if (angle_diff > chase_turn_rate) {
    angle_diff = chase_turn_rate;
  } else if (angle_diff < -chase_turn_rate) {
    angle_diff = -chase_turn_rate;
  }

  police_car->angle += angle_diff;

  if (police_car->angle > 2.0f * PI) {
    police_car->angle -= 2.0f * PI;
  } else if (police_car->angle < 0.0f) {
    police_car->angle += 2.0f * PI;
  }

  police_car->speed += chase_accel;
  if (police_car->speed > chase_max_speed) {
    police_car->speed = chase_max_speed;
  }
  police_car->speed *= chase_drag;

  next_x = police_car->x + cosf(police_car->angle) * police_car->speed;
  next_y = police_car->y - sinf(police_car->angle) * police_car->speed;

  if (!check_collision(next_x, police_car->y)) {
    police_car->x = next_x;
  } else {
    police_car->angle += PI * 0.35f;
    police_car->speed *= 0.5f;
    police_target_x[index] = player.x;
    police_target_y[index] = player.y;
  }

  if (!check_collision(police_car->x, next_y)) {
    police_car->y = next_y;
  } else {
    police_car->angle -= PI * 0.25f;
    police_car->speed *= 0.5f;
    police_target_x[index] = player.x;
    police_target_y[index] = player.y;
  }

  ensure_police_car_valid(index);

  if (check_player_police_collision_for_index(index)) {
    police_car->angle =
        atan2f(-(player.y - police_car->y), player.x - police_car->x);
  }
}

void update_police_car(void) {
  int i;

  sync_police_cars_to_score();
  update_chase_distance_map();
  for (i = 0; i < MAX_POLICE_CARS; i++) {
    update_single_police_car(i);
  }
}

// Update the camera position to center on the player
void update_camera(void) {
  camera_x = player.x - (SCREEN_W / 2.0f);
  camera_y = player.y - (SCREEN_H / 2.0f);

  camera_x = clamp_float(camera_x, 0.0f, (float)(WORLD_W - SCREEN_W));
  camera_y = clamp_float(camera_y, 0.0f, (float)(WORLD_H - SCREEN_H));
}

// Encode a single digit (0-9) as the corresponding 7-segment display code for
// the HEX displays
unsigned char encode_hex_digit(int digit) {
  static const unsigned char hex_codes[16] = {
      0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
      0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};

  if (digit < 0) {
    return 0x00;
  }

  return hex_codes[digit & 0x0F];
}

// Update HEX display to show current score
void update_score_display(void) {
  int value = score;
  int digit0 = value % 10;
  int digit1;
  int digit2;
  int digit3;
  int digit4;
  int digit5;
  int hex30;
  int hex54;

  value /= 10;
  digit1 = (value > 0) ? (value % 10) : -1;
  value /= 10;
  digit2 = (value > 0) ? (value % 10) : -1;
  value /= 10;
  digit3 = (value > 0) ? (value % 10) : -1;
  value /= 10;
  digit4 = (value > 0) ? (value % 10) : -1;
  value /= 10;
  digit5 = (value > 0) ? (value % 10) : -1;

  hex30 = (int)encode_hex_digit(digit0) | ((int)encode_hex_digit(digit1) << 8) |
          ((int)encode_hex_digit(digit2) << 16) |
          ((int)encode_hex_digit(digit3) << 24);
  hex54 = (int)encode_hex_digit(digit4) | ((int)encode_hex_digit(digit5) << 8);

  *HEX3_HEX0_ptr = hex30;
  *HEX5_HEX4_ptr = hex54;
}

// Update the board LEDs so the number of lit LEDs matches remaining lives
void update_lives_display(void) {
  unsigned int led_mask;

  if (player_lives <= 0) {
    led_mask = 0u;
  } else if (player_lives >= MAX_LIVES) {
    led_mask = (1u << MAX_LIVES) - 1u;
  } else {
    led_mask = (1u << player_lives) - 1u;
  }

  *LEDR_ptr = (int)led_mask;
}

// Draw the world, with player and police car on top
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
  draw_health_bar();
}

unsigned char get_glyph_row(char ch, int row) {
  switch (ch) {
    case 'A': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x11, 0x1F,
                                            0x11, 0x11, 0x11};
      return rows[row];
    }
    case 'B': {
      static const unsigned char rows[7] = {0x1E, 0x11, 0x11, 0x1E,
                                            0x11, 0x11, 0x1E};
      return rows[row];
    }
    case 'C': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x10, 0x10,
                                            0x10, 0x11, 0x0E};
      return rows[row];
    }
    case 'E': {
      static const unsigned char rows[7] = {0x1F, 0x10, 0x10, 0x1E,
                                            0x10, 0x10, 0x1F};
      return rows[row];
    }
    case 'G': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x10, 0x17,
                                            0x11, 0x11, 0x0E};
      return rows[row];
    }
    case 'M': {
      static const unsigned char rows[7] = {0x11, 0x1B, 0x15, 0x15,
                                            0x11, 0x11, 0x11};
      return rows[row];
    }
    case 'N': {
      static const unsigned char rows[7] = {0x11, 0x19, 0x15, 0x13,
                                            0x11, 0x11, 0x11};
      return rows[row];
    }
    case 'O': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x11, 0x11,
                                            0x11, 0x11, 0x0E};
      return rows[row];
    }
    case 'R': {
      static const unsigned char rows[7] = {0x1E, 0x11, 0x11, 0x1E,
                                            0x14, 0x12, 0x11};
      return rows[row];
    }
    case 'S': {
      static const unsigned char rows[7] = {0x0F, 0x10, 0x10, 0x0E,
                                            0x01, 0x01, 0x1E};
      return rows[row];
    }
    case 'T': {
      static const unsigned char rows[7] = {0x1F, 0x04, 0x04, 0x04,
                                            0x04, 0x04, 0x04};
      return rows[row];
    }
    case 'U': {
      static const unsigned char rows[7] = {0x11, 0x11, 0x11, 0x11,
                                            0x11, 0x11, 0x0E};
      return rows[row];
    }
    case 'V': {
      static const unsigned char rows[7] = {0x11, 0x11, 0x11, 0x11,
                                            0x11, 0x0A, 0x04};
      return rows[row];
    }
    case 'P': {
      static const unsigned char rows[7] = {0x1E, 0x11, 0x11, 0x1E,
                                            0x10, 0x10, 0x10};
      return rows[row];
    }
    case 'L': {
      static const unsigned char rows[7] = {0x10, 0x10, 0x10, 0x10,
                                            0x10, 0x10, 0x1F};
      return rows[row];
    }
    case 'I': {
      static const unsigned char rows[7] = {0x0E, 0x04, 0x04, 0x04,
                                            0x04, 0x04, 0x0E};
      return rows[row];
    }
    case 'F': {
      static const unsigned char rows[7] = {0x1F, 0x10, 0x10, 0x1E,
                                            0x10, 0x10, 0x10};
      return rows[row];
    }
    case 'H': {
      static const unsigned char rows[7] = {0x11, 0x11, 0x11, 0x1F,
                                            0x11, 0x11, 0x11};
      return rows[row];
    }
    case 'D': {
      static const unsigned char rows[7] = {0x1E, 0x11, 0x11, 0x11,
                                            0x11, 0x11, 0x1E};
      return rows[row];
    }
    case ':': {
      static const unsigned char rows[7] = {0x00, 0x04, 0x04, 0x00,
                                            0x04, 0x04, 0x00};
      return rows[row];
    }
    case '0': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x13, 0x15,
                                            0x19, 0x11, 0x0E};
      return rows[row];
    }
    case '1': {
      static const unsigned char rows[7] = {0x04, 0x0C, 0x04, 0x04,
                                            0x04, 0x04, 0x0E};
      return rows[row];
    }
    case '2': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x01, 0x02,
                                            0x04, 0x08, 0x1F};
      return rows[row];
    }
    case '3': {
      static const unsigned char rows[7] = {0x1E, 0x01, 0x01, 0x0E,
                                            0x01, 0x01, 0x1E};
      return rows[row];
    }
    case '4': {
      static const unsigned char rows[7] = {0x02, 0x06, 0x0A, 0x12,
                                            0x1F, 0x02, 0x02};
      return rows[row];
    }
    case '5': {
      static const unsigned char rows[7] = {0x1F, 0x10, 0x10, 0x1E,
                                            0x01, 0x01, 0x1E};
      return rows[row];
    }
    case '6': {
      static const unsigned char rows[7] = {0x0E, 0x10, 0x10, 0x1E,
                                            0x11, 0x11, 0x0E};
      return rows[row];
    }
    case '7': {
      static const unsigned char rows[7] = {0x1F, 0x01, 0x02, 0x04,
                                            0x08, 0x08, 0x08};
      return rows[row];
    }
    case '8': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x11, 0x0E,
                                            0x11, 0x11, 0x0E};
      return rows[row];
    }
    case '9': {
      static const unsigned char rows[7] = {0x0E, 0x11, 0x11, 0x0F,
                                            0x01, 0x01, 0x0E};
      return rows[row];
    }
    default:
      return 0x00;
  }
}

void draw_text(int x, int y, const char* text, int scale, short int color) {
  int cursor_x = x;
  int index;
  int row;
  int col;

  for (index = 0; text[index] != '\0'; index++) {
    if (text[index] == ' ') {
      cursor_x += 6 * scale;
      continue;
    }

    for (row = 0; row < 7; row++) {
      unsigned char bits = get_glyph_row(text[index], row);
      for (col = 0; col < 5; col++) {
        if ((bits & (1u << (4 - col))) != 0u) {
          draw_rect(cursor_x + col * scale, y + row * scale, scale, scale,
                    color);
        }
      }
    }

    cursor_x += 6 * scale;
  }
}

int get_text_width(const char* text, int scale) {
  int width = 0;
  int index;

  for (index = 0; text[index] != '\0'; index++) {
    width += 6 * scale;
  }

  if (width > 0) {
    width -= scale;
  }

  return width;
}

void draw_number(int x, int y, int value, int scale, short int color) {
  char digits[12];
  int length = 0;
  int index;

  if (value == 0) {
    digits[length++] = '0';
  } else {
    while (value > 0 && length < 11) {
      digits[length++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }

  for (index = 0; index < length / 2; index++) {
    char temp = digits[index];
    digits[index] = digits[length - 1 - index];
    digits[length - 1 - index] = temp;
  }
  digits[length] = '\0';

  draw_text(x, y, digits, scale, color);
}

void draw_game_over_screen(void) {
  const char* title = "GAME OVER";
  const char* current_label = "CURRENT SCORE:";
  const char* best_label = "BEST SCORE:";
  const char* restart_label = "PRESS R TO RESTART";
  const int title_scale = 3;
  const int body_scale = 2;
  const int restart_scale = 1;
  const int title_height = 7 * title_scale;
  const int body_height = 7 * body_scale;
  const int restart_height = 7 * restart_scale;
  const int gap = 10;
  const int content_height = title_height + body_height + body_height +
                             body_height + body_height + restart_height +
                             gap * 5;
  char score_digits[12];
  char best_digits[12];
  int score_length = 0;
  int best_length = 0;
  int start_y;
  int title_y;
  int current_label_y;
  int score_y;
  int best_label_y;
  int best_score_y;
  int restart_y;
  int index;

  if (score == 0) {
    score_digits[score_length++] = '0';
  } else {
    int value = score;
    while (value > 0 && score_length < 11) {
      score_digits[score_length++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }
  for (index = 0; index < score_length / 2; index++) {
    char temp = score_digits[index];
    score_digits[index] = score_digits[score_length - 1 - index];
    score_digits[score_length - 1 - index] = temp;
  }
  score_digits[score_length] = '\0';

  if (best_score == 0) {
    best_digits[best_length++] = '0';
  } else {
    int value = best_score;
    while (value > 0 && best_length < 11) {
      best_digits[best_length++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }
  for (index = 0; index < best_length / 2; index++) {
    char temp = best_digits[index];
    best_digits[index] = best_digits[best_length - 1 - index];
    best_digits[best_length - 1 - index] = temp;
  }
  best_digits[best_length] = '\0';

  start_y = (SCREEN_H - content_height) / 2;
  title_y = start_y;
  current_label_y = title_y + title_height + gap;
  score_y = current_label_y + body_height + gap;
  best_label_y = score_y + body_height + gap;
  best_score_y = best_label_y + body_height + gap;
  restart_y = best_score_y + body_height + gap;

  clear_screen(BLACK);
  draw_text((SCREEN_W - get_text_width(title, title_scale)) / 2, title_y, title,
            title_scale, RED);
  draw_text((SCREEN_W - get_text_width(current_label, body_scale)) / 2,
            current_label_y, current_label, body_scale, WHITE);
  draw_text((SCREEN_W - get_text_width(score_digits, body_scale)) / 2, score_y,
            score_digits, body_scale, YELLOW);
  draw_text((SCREEN_W - get_text_width(best_label, body_scale)) / 2,
            best_label_y, best_label, body_scale, WHITE);
  draw_text((SCREEN_W - get_text_width(best_digits, body_scale)) / 2,
            best_score_y, best_digits, body_scale, GREEN);
  draw_text((SCREEN_W - get_text_width(restart_label, restart_scale)) / 2,
            restart_y, restart_label, restart_scale, LIGHT_GRAY);
}

// Draw a segmented health bar on the VGA display
void draw_health_bar(void) {
  const int border = 1;
  const int padding = 3;
  const int gap = 2;
  const int segment_w = 8;
  const int bar_h = 12;
  const int segments_w = MAX_LIVES * segment_w + (MAX_LIVES - 1) * gap;
  const int panel_x = 8;
  const int panel_y = 8;
  const int panel_w = segments_w + (padding * 2) + (border * 2);
  const int panel_h = bar_h + (padding * 2) + (border * 2);
  const int bar_x = panel_x + border + padding;
  const int bar_y = panel_y + border + padding;
  int segment;
  short int border_color = WHITE;
  short int fill_color = GREEN;

  if (player_lives <= 2) {
    fill_color = RED;
  } else if (player_lives < MAX_LIVES) {
    fill_color = YELLOW;
  }

  if (police_freeze_frames > 0) {
    border_color = BLUE;
  }

  draw_rect(panel_x, panel_y, panel_w, panel_h, BLACK);
  draw_rect(panel_x, panel_y, panel_w, 1, border_color);
  draw_rect(panel_x, panel_y + panel_h - 1, panel_w, 1, border_color);
  draw_rect(panel_x, panel_y, 1, panel_h, border_color);
  draw_rect(panel_x + panel_w - 1, panel_y, 1, panel_h, border_color);

  for (segment = 0; segment < MAX_LIVES; segment++) {
    int segment_x = bar_x + segment * (segment_w + gap);
    short int segment_color = (segment < player_lives) ? fill_color : DARK_GRAY;

    draw_rect(segment_x, bar_y, segment_w, bar_h, segment_color);
  }
}

// Draw a single tile at the specified column and row, with the top-left corner
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

  if (tile == TILE_BUILDING) {
    draw_rect(screen_x + 4, screen_y + 4, TILE_SIZE - 8, TILE_SIZE - 8,
              DARK_GRAY);
  } else if (tile == TILE_ROAD && cash_pickups[row][col]) {
    draw_rect(screen_x + 13, screen_y + 13, 6, 6, YELLOW);
  }
}

void draw_player(void) {
  draw_filled_car(player.x, player.y, player.angle, WHITE, RED);
}

void draw_single_police_car(int index) {
  if (!police_cars[index].active) {
    return;
  }

  draw_filled_car(police_cars[index].x, police_cars[index].y,
                  police_cars[index].angle, BLUE, RED);
}

void draw_police_car(void) {
  int i;

  for (i = 0; i < MAX_POLICE_CARS; i++) {
    draw_single_police_car(i);
  }
}

void draw_filled_car(float world_x, float world_y, float angle,
                     short int body_color, short int nose_color) {
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
      float px_float = forward_x * (float)along + right_x * (float)across;
      float py_float = forward_y * (float)along + right_y * (float)across;
      int px = cx + (int)(px_float + 0.5f);
      int py = cy + (int)(py_float + 0.5f);
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

int world_to_screen_x(float world_x) { return (int)(world_x - camera_x); }

int world_to_screen_y(float world_y) { return (int)(world_y - camera_y); }
