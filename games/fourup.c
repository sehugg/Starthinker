
#include "ai.h"

//#define USE_NUM_TURNS

#define BOARDX 7
#define BOARDY 6

typedef uint64_t BoardMask;

typedef struct 
{
  BoardMask pieces[MAX_PLAYERS]; // bit set for each player
  uint8_t columns[BOARDX];
} GameState;

void init_game(GameState* state)
{
  assert(BOARDX*BOARDY <= 64);
  memset(state, 0, sizeof(GameState));
}

// bit index of x,y position
#define BI(x,y) ((y)*BOARDX+(x))
// bit mask (1 bit set)
#define BM(x,y) (((x)>=0&&(y)>=0&&(x)<BOARDX&&(y)<BOARDY)?(((BoardMask)1)<<BI(x,y)):0)
// all positions
#define ALLMASK RANGE(0,BOARDX*BOARDY)

// returns player index or -1 for no occupancy
int get_piece_at(const GameState* state, int x, int y)
{
  int s = y*BOARDX + x;
  int i;
  for (i=0; i<MAX_PLAYERS; i++)
  {
    if (state->pieces[i] & (1ull<<s))
      return i;
  }
  return -1;
}

BoardMask get_occupancy(const GameState* state)
{
  // 4 players max
  return state->pieces[0] | state->pieces[1] | state->pieces[2] | state->pieces[3];
}

// s = bit index
void set_piece_at(const GameState* state, int s, int player)
{
  DEBUG("player %d set %d (%"PRIx64")\n", player, s, state->pieces[player]);
  assert(!(get_occupancy(state) & (1ull<<s)));
  SET(state->pieces[player], state->pieces[player] | (1ull<<s));
}

void print_board(const GameState* state)
{
  int y,x;
  static const char* CH = ".XOAZ";
  printf("\nBOARD (Player %d):\n", ai_current_player());
  for (y=0; y<BOARDY; y++)
  {
    printf("\n\t");
    for (x=0; x<BOARDX; x++)
    {
      printf(" %c", CH[get_piece_at(state,x,BOARDY-1-y)+1]);
    }
  }
  printf("\n\n");
}

int player_controls_all(const GameState* state, BoardMask mask)
{
  int i;
  for (i=0; i<num_players; i++)
  {
    if ((state->pieces[i] & mask) == mask) // all pieces owned by player i?
      return i;
  }
  return -1;
}

// exit current function if player wins
#define CHECK_WIN(mask) { int result = player_controls_all(state,mask); if (result>=0) { DEBUG("CHECK_WIN: %s\n", #mask); return result; } }

const BoardMask ALL = ALLMASK;
const BoardMask HORIZ = BM(0,0) + BM(1,0) + BM(2,0) + BM(3,0);
const BoardMask VERT  = BM(0,0) + BM(0,1) + BM(0,2) + BM(0,3);
const BoardMask DIAG1 = BM(0,0) + BM(1,1) + BM(2,2) + BM(3,3);
const BoardMask DIAG2 = BM(3,0) + BM(2,1) + BM(1,2) + BM(0,3);

int player_won(const GameState* state)
{
  BoardMask horiz = HORIZ;
  BoardMask vert = VERT;
  BoardMask diag1 = DIAG1;
  BoardMask diag2 = DIAG2;
  // iterate thru columns 0-3
  int i;
  BoardMask mask;
  for (int x=0; x<BOARDX-4; x++)
  {
    // check diagonal wins
    CHECK_WIN(diag1);
    CHECK_WIN(diag1 << (BOARDX*1));
    CHECK_WIN(diag1 << (BOARDX*2));
    CHECK_WIN(diag2);
    CHECK_WIN(diag2 << (BOARDX*1));
    CHECK_WIN(diag2 << (BOARDX*2));
    // check horizontal wins
    mask = horiz;
    for (i=0; i<BOARDY; i++)
    {
      CHECK_WIN(mask+0);
      mask <<= BOARDX; // move down one row
    }
    diag1 <<= 1;
    diag2 <<= 1;
    horiz <<= 1;
  }
  // check vertical wins
  mask = vert;
  for (i=0; i<BOARDX; i++)
  {
    CHECK_WIN(0+mask);
    CHECK_WIN(mask << (BOARDX*1));
    CHECK_WIN(mask << (BOARDX*2));
    mask <<= 1; // move over one column
  }
  return -1;
}

bool play_turn(const GameState* state);

int make_move(const void* pstate, ChoiceIndex column)
{
  const GameState* state = pstate;
  assert(column>=0 && column<BOARDX);
  DEBUG("Plyr %d move to column %d\n", ai_current_player(), column);

  // set player piece on board
  int y = state->columns[column];
  int index = y*BOARDX + column;
  set_piece_at(state, index, ai_current_player());
  INC(state->columns[column]);
  
  // did we win?
  int winner = player_won(state);
  if (winner >= 0)
  {
    DEBUG("Plyr %d wins\n", winner);
    ai_set_player_score(winner, MAX_SCORE);
    ai_game_over();
    return 1;
  }

  // next player
  if (ai_next_player())
  {
    play_turn(state);
  }
  return 1;
}

bool play_turn(const GameState* state)
{
  BoardMask mask = 0;
  for (int x=0; x<BOARDX; x++)
  {
    if (state->columns[x] < BOARDY)
      mask |= (1 << x);
  }
  if (mask == 0)
  {
    DEBUG("Draw (P%d)\n", ai_current_player());
    ai_game_over();
    return false;
  }
  else 
  {
    return ai_choice(state, sizeof(GameState), make_move, 0, mask) != 0;
  }
}

void play_game(const GameState* state)
{
  print_board(state);
  while (play_turn(state))
  {
    print_board(state);
    if (player_won(state) >= 0)
      break;
  }
}

int main(int argc, char** argv)
{
  GameState state;
  
  ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 14;
  defaults.max_walk_level = 50;
  ai_init(&defaults);

  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
