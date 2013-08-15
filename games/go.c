
#include "ai.h"

static char* CHARS_PER_PIECE = ".XOAB";

typedef enum {
  NONE,
  WHITE,
  BLACK,
  RED,
  BLUE,
  WALL
} CellType;

#define BOARDX 9
#define BOARDY 9

typedef uint32_t RowMask;

typedef struct
{
  uint8_t board[BOARDY+2][32]; // padding on all sides, so coords are all 1-indexed
  int consecutive_passes;
  HashCode last_board_hash;
} GameState;

//

#define COORDFMT "%c%d"
static char* COL_CHARS = " ABCDEFGHJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuv";
#define COORDS(x,y) COL_CHARS[x], y

int get_stone(const GameState* state, int x, int y)
{
  return ((int)state->board[y][x]) - 1;
}

void place_stone(const GameState* state, int player, int x, int y)
{
  assert(state->board[y][x] == 0);
  SET(state->board[y][x], player + 1);
}

int remove_stone(const GameState* state, int x, int y)
{
  int p = state->board[y][x];
  assert(p);
  SET(state->board[y][x], 0);
  return p;
}

void print_board(const GameState* state)
{
  printf("\nBOARD:\n\n");
  printf("    A B C D E F G H J K L M N O P Q R S T\n\n");
  int x,y;
  for (y=BOARDY; y>=1; y--)
  {
    printf(" %2d ", y);
    for (x=1; x<=BOARDX; x++)
    {
      char ch;
      int piece = state->board[y][x];
      ch = CHARS_PER_PIECE[piece];
      if (!piece && (y==4||y==10||y==16) && (x==4||x==10||x==16))
        ch = '+';
      printf("%c ", ch);
    }
    printf("\n");
  }
  printf("    A B C D E F G H J K L M N O P Q R S T\n\n");
}

  
static int move_row = 0;

void play_turn(const GameState* state);

static RowMask visited_rows[BOARDY+2];
static int stone_count;

int has_liberties(const GameState* state, int player, int x, int y)
{
  // how many liberties?
  int p = get_stone(state, x, y);
  if (p == player)
  {
    // don't visit cells twice
    int bm = 1<<x;
    if (visited_rows[y] & bm)
    {
      return 0;
    }
    visited_rows[y] |= bm;
    stone_count++;
    // this is our piece, so count our liberties
    //DEBUG("Counting liberties for P%d @ " COORDFMT "\n", player, COORDS(x,y));
    return
      has_liberties(state, player, x-1, y) ||
      has_liberties(state, player, x+1, y) ||
      has_liberties(state, player, x, y-1) ||
      has_liberties(state, player, x, y+1);
  }
  else if (p < 0)
  {
    return 1; // nothing there, 1 liberty
  }
  else
  {
    return 0; // enemy or edge, 0 liberties
  }
}

int capture_stones(const GameState* state, int x, int y)
{
  // don't visit cells twice (opposite of has_liberties)
  int bm = 1<<x;
  if (!(visited_rows[y] & bm))
  {
    return 0;
  }
  //DEBUG("Removed stone @ " COORDFMT "\n", COORDS(x,y));
  remove_stone(state, x, y);
  visited_rows[y] &= ~bm;
  return
    capture_stones(state, x-1, y) +
    capture_stones(state, x+1, y) +
    capture_stones(state, x, y-1) +
    capture_stones(state, x, y+1);
}

int find_capture(const GameState* state, int player, int x, int y)
{
  if (get_stone(state, x, y) != player)
    return 0;
    
  int total = 0;
  stone_count = 0;
  memset(visited_rows, 0, sizeof(visited_rows));
  if (!has_liberties(state, player, x, y) && stone_count)
  {
    DEBUG("Captured %d stones from player %d starting @ " COORDFMT "\n", stone_count, player, COORDS(x,y));
    //print_board(state);
    capture_stones(state, x, y);
    //print_board(state);
    if (ai_current_player() != player)
    {
      ai_add_player_score(ai_current_player(), stone_count*100);
    }
    return stone_count;
  } else
    return 0;
}

int make_move(const void* pstate, ChoiceIndex x)
{
  const GameState* state = pstate;
  int y = move_row;
  int player = ai_current_player();
  // place the stone
  DEBUG("Player %d played @ " COORDFMT "\n", player, COORDS(x,y));
  place_stone(state, player, x, y);
  // look for captures in adjacent stones
  // do other players first
  for (int p=0; p<num_players; p++)
  {
    if (p == player)
      continue;
    find_capture(state, p, x-1, y);
    find_capture(state, p, x+1, y);
    find_capture(state, p, x, y-1);
    find_capture(state, p, x, y+1);
  }
  // now do self-capture
  // we make it illegal, so return 0 if this option is taken
  if (find_capture(state, player, x, y))
    return 0;
  // we moved, so set state.consecutive_passes to 0
  if (state->consecutive_passes)
  {
    SET(state->consecutive_passes, 0);
  }
  // ko rule: don't repeat previous position (TODO: use more reliable hash?)
  HashCode hash = ai_current_hash();
  if (hash == state->last_board_hash)
  {
    DEBUG("Can't repeat previous move: %x == %x\n", hash, state->last_board_hash);
    return 0;
  }
  SET(state->last_board_hash, hash);
  // next player  
  if (ai_next_player())
    play_turn(state); // if in search mode
  return 1;
}

int surrounds_area(const GameState* state, int player, int x, int y)
{
  // how many liberties?
  int p = get_stone(state, x, y);
  // flood-fill empty areas
  if (p < 0)
  {
    // don't visit cells twice
    int bm = 1<<x;
    if (visited_rows[y] & bm)
    {
      return 0;
    }
    visited_rows[y] |= bm;
    stone_count++;
    // this is our piece, so count our liberties
    //DEBUG("Counting liberties for P%d @ " COORDFMT "\n", player, COORDS(x,y));
    return
      surrounds_area(state, player, x-1, y) &&
      surrounds_area(state, player, x+1, y) &&
      surrounds_area(state, player, x, y-1) &&
      surrounds_area(state, player, x, y+1);
  }
  else
  {
    return p == player || p == WALL-1; // we control an area if it's bordered by our piece or by the wall
  }
}

void final_scoring(const GameState* state)
{
  for (int player=0; player<num_players; player++)
  {
    int score = 0;
    memset(visited_rows, 0, sizeof(visited_rows));
    stone_count = 0;
    for (int y=1; y<=BOARDY; y++)
    {
      for (int x=1; x<=BOARDX; x++)
      {
        score += surrounds_area(state, player, x, y);
      }
    }
    ai_add_player_score(player, score*100);
  }
}

// player passes, return 'true' if game is over
bool player_passes(const GameState* state)
{
  INC(state->consecutive_passes);
  ai_add_player_score(ai_current_player(), -100);
  DEBUG("Player %d passed (%d) mode %d\n", ai_current_player(), state->consecutive_passes, ai_get_mode());
  if (state->consecutive_passes >= num_players)
  {
    final_scoring(state);
    ai_game_over();
    return true;
  }
  else
    return false;
}

RowMask get_occupied_row(const GameState* state, int row)
{
  RowMask m = 0;
  for (int i=1; i<=BOARDX; i++)
    if (state->board[row][i])
      m |= CHOICE(i);
  return m;
}

int choose_row(const void* pstate, ChoiceIndex row)
{
  const GameState* state = pstate;
  // did we pass?
  if (row == 0)
  {
    if (player_passes(state))
      ; // game over
    else if (ai_next_player())
      play_turn(state); // if in search mode
    return 1;
  }
  // choose an x-coordinate from unoccupied squares
  RowMask open = get_occupied_row(state, row) ^ RANGE(1,BOARDX+1);
  if (open != 0)
  {
    SETGLOBAL(move_row, row);
    // try all moves, are any valid?
    return ai_choice(state, 0, make_move, 0, open);
  }
  return 0;
}

void play_turn(const GameState* state)
{
  // player chooses row or passes (0)
  // TODO: don't include full rows
  ai_choice(state, 0, choose_row, 0, RANGE(0,BOARDY+1));
}

void play_game(const GameState* state)
{
  while (state->consecutive_passes < num_players)
  {
    print_board(state);
    play_turn(state);
  }
}

void init_game(GameState* state)
{
  memset(state, 0, sizeof(GameState));
  for (int x=0; x<=BOARDX+1; x++)
  {
    state->board[0][x] = WALL;
    state->board[BOARDY+1][x] = WALL;
  }
  for (int y=0; y<=BOARDX+1; y++)
  {
    state->board[y][0] = WALL;
    state->board[y][BOARDX+1] = WALL;
  }
}

int main(int argc, char** argv)
{
  int argi = ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 10;
  defaults.max_walk_level = BOARDX*BOARDY*2;
  ai_init(&defaults);

  GameState state;
  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
