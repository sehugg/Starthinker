
#include "ai.h"

//#define USE_NUM_TURNS

#define BOARDX 3
#define BOARDY 3

typedef uint32_t BoardMask;

typedef struct 
{
  BoardMask pieces[MAX_PLAYERS]; // bit set for each player
} GameState;

void init_game(GameState* state)
{
  assert(BOARDX*BOARDY <= 32);
  memset(state, 0, sizeof(GameState));
}

// bit index of x,y position
#define BI(x,y) ((y)*BOARDX+(x))
// bit mask (1 bit set)
#define BM(x,y) (((x)>=0&&(y)>=0&&(x)<BOARDX&&(y)<BOARDY)?(1<<BI(x,y)):0)
// all positions
#define ALLMASK RANGE(0,BOARDX*BOARDY)

// returns player index or -1 for no occupancy
int get_piece_at(const GameState* state, int x, int y)
{
  int s = y*BOARDX + x;
  int i;
  for (i=0; i<MAX_PLAYERS; i++)
  {
    if (state->pieces[i] & (1<<s))
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
  DEBUG("player %d set %d (%x)\n", player, s, state->pieces[player]);
  assert(!(get_occupancy(state) & (1<<s)));
  SET(state->pieces[player], state->pieces[player] | (1<<s));
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
      printf(" %c", CH[get_piece_at(state,x,y)+1]);
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
#define CHECK_WIN(mask) { int result = player_controls_all(state,mask); if (result>=0) return result; }

// http://www.se16.info/hgb/tictactoe.htm
int player_won(const GameState* state)
{
  const BoardMask all = ALLMASK;
  const BoardMask horiz = BM(0,0) + BM(1,0) + BM(2,0) + BM(3,0) + BM(4,0);
  const BoardMask vert  = BM(0,0) + BM(0,1) + BM(0,2) + BM(0,3) + BM(0,4);
  const BoardMask diag1 = BM(0,0) + BM(1,1) + BM(2,2) + BM(3,3) + BM(4,4);
  const int s = 5-BOARDX;
  const BoardMask diag2 = BM(4-s,0) + BM(3-s,1) + BM(2-s,2) + BM(1-s,3) + BM(0-s,4);

  //printf("%x %x %x %x %x\n", all, horiz, vert, diag1, diag2);
  int i,mask;
  // check diagonal wins
  CHECK_WIN(diag1);
  CHECK_WIN(diag2);
  // check horizontal wins
  mask = horiz;
  for (i=0; i<BOARDY; i++)
  {
    CHECK_WIN(mask);
    mask <<= BOARDX; // move down one row
  }
  // check vertical wins
  mask = vert;
  for (i=0; i<BOARDX; i++)
  {
    CHECK_WIN(mask);
    mask <<= 1; // move over one column
  }
  return -1;
}

bool play_turn(const GameState* state);

int make_move(const void* pstate, ChoiceIndex index)
{
  const GameState* state = pstate;
  assert(index>=0 && index<BOARDX*BOARDY);
    
  DEBUG("Plyr %d move to %d,%d\n", ai_current_player(), index % BOARDX, index / BOARDX);
  
  // set player piece on board
  set_piece_at(state, index, ai_current_player());
  // TODO; why does this change things?
  
  // did we win?
  int winner = player_won(state);
  if (winner >= 0)
  {
    DEBUG("Plyr %d wins\n", winner);
    ai_set_player_score(winner, MAX_SCORE);
    ai_game_over();
    //print_board(state);
    //return GAME_OVER + winner;
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
  BoardMask mask = ALLMASK ^ get_occupancy(state);
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
  while (play_turn(state) && player_won(state) < 0)
  {
    print_board(state);
  }
  print_board(state);
}

int main(int argc, char** argv)
{
  GameState state;
  
  ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 9;
  ai_init(&defaults);

  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
