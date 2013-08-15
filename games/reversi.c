
#include "ai.h"

// TODO: verify w/ perth (http://www.aartbik.com/MISC/reversi.html) level 8 seems wrong

//#define USE_NUM_TURNS

#define BOARDX 8
#define BOARDY 8

typedef uint64_t BoardMask;

// bit index of x,y position
#define BI(x,y) ((y)*BOARDX+(x))
// bit mask (1 bit set)
#define BM(x,y) (((x)>=0&&(y)>=0&&(x)<BOARDX&&(y)<BOARDY)?CHOICE(BI(x,y)):0)
// line of 8 squares starting @ x0,y0 and with direction xd,yd
#define LINE8(x0,y0,xd,yd) (BM(x0+xd*0,y0+yd*0) | BM(x0+xd*1,y0+yd*1) | BM(x0+xd*2,y0+yd*2) | BM(x0+xd*3,y0+yd*3) | BM(x0+xd*4,y0+yd*4) | BM(x0+xd*5,y0+yd*5) | BM(x0+xd*6,y0+yd*6) | BM(x0+xd*7,y0+yd*7))

// game state
// - bitboard for each plyaer
// - # of consecutive passes, 2 means game over
typedef struct 
{
  BoardMask pieces[MAX_PLAYERS]; // bit set for each player
  uint8_t consecutive_passes;
} GameState;

//

// setup default game state and initial board
void init_game(GameState* state)
{
  assert(BOARDX*BOARDY <= 64);
  memset(state, 0, sizeof(GameState));
  
  state->pieces[0] = BM(3,3)|BM(4,4);
  state->pieces[1] = BM(3,4)|BM(4,3);
}

// returns player index or -1 for no occupancy
int get_piece_at(const GameState* state, int x, int y)
{
  assert(x>=0&&y>=0&&x<BOARDX&&y<BOARDY);
  int s = y*BOARDX + x;
  int i;
  for (i=0; i<MAX_PLAYERS; i++)
  {
    if (state->pieces[i] & (1ull<<s))
      return i;
  }
  return -1;
}

// returns bitmask of squares occupied by either player
BoardMask get_occupancy(const GameState* state)
{
  // 4 players max
  return state->pieces[0] | state->pieces[1] | state->pieces[2] | state->pieces[3];
}

// set piece @ position s = bit index (use macro BI) overriding another player if neccessary
// NOTE: 'state' is not const, so we can only use this with local copies of the state
void set_piece_at(GameState* state, int s, int player)
{
  DEBUG("player %d set %d (%llx)\n", player, s, state->pieces[player]);
  BoardMask mask = (1ull<<s);
  for (int i=0; i<num_players; i++)
  {
    if (i == player)
    {
      state->pieces[i] |= mask;
    }
    else if (state->pieces[i] & mask)
    {
      state->pieces[i] &= ~mask;
    }
  }
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

// returns player index if player controls all squares in 'mask'
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

// player passes, return 'true' if game is over
bool player_passes(const GameState* state)
{
  INC(state->consecutive_passes);
  DEBUG("Player %d passed (%d) mode %d\n", ai_current_player(), state->consecutive_passes, ai_get_mode());
  if (state->consecutive_passes >= num_players)
  {
    ai_game_over();
    return true;
  }
  else
    return false;
}

void play_turn(const GameState* state);

int count_flippable_pieces(const GameState* state, int player, int x, int y, int dx, int dy)
{
  int count = 0;
  int piece;
  
  // find end of line
  do {
    x += dx;
    y += dy;
    if (x<0 || y<0 || x>=BOARDX || y>=BOARDY)
      return 0; // fell off edge
    piece = get_piece_at(state,x,y);
    if (piece < 0)
      return 0;
    count++;
  } while (piece != player);

  // we need at least two pieces (one opponent and one of ours)
  return (count < 2) ? 0 : count-1;
}  

// attempt to flip pieces of a given player starting @ x,y and in direction dx,dy
// returns # of pieces flipped of 0 if invalid move
int flip_pieces(GameState* state, int player, int x, int y, int dx, int dy)
{
  int count = 0;
  int piece;
  
  // find end of line
  do {
    x += dx;
    y += dy;
    if (x<0 || y<0 || x>=BOARDX || y>=BOARDY)
      return 0; // fell off edge
    piece = get_piece_at(state,x,y);
    if (piece < 0)
      return 0;
    count++;
  } while (piece != player);

  // we need at least two pieces (one opponent and one of ours)
  if (count < 2)
    return 0;
  
  // flip the pieces
  for (int i=1; i<count; i++)
  {
    x -= dx;
    y -= dy;
    DEBUG("Set %d,%d = %d\n", x, y, player);
    set_piece_at(state, BI(x,y), player);
  }
  return count-1;
}

// move callback function
int make_move(const void* pstate, ChoiceIndex index)
{
  const GameState* state = pstate;
  // make a copy of the original state (it'll be faster!)
  GameState tmp = *state;
  
  assert(index>=0 && index<BOARDX*BOARDY);
  int player = ai_current_player();
  int x = index % BOARDX;
  int y = index / BOARDY;
  DEBUG("Plyr %d move to %d,%d\n", player, x, y);

  // flip pieces
  int count = 0;
  for (int dy=-1; dy<=1; dy++)
  {
    for (int dx=-1; dx<=1; dx++)
    {
      if (dx==0 && dy==0)
        continue;
      int n = flip_pieces(&tmp, player, x, y, dx, dy);
      if (n > 0)
        count += n;
      DEBUG("  [%d,%d %+d,%+d] = %d pieces\n", x, y, dx, dy, n);
    }
  }
  
  // no moves? forget it
  DEBUG("Flipped %d pieces @ %d,%d\n", count, x, y);
  if (count == 0)
  {
    return 0;
  }

  // set player piece on board
  // note that the 'state' variable is non-const
  // so we must remember to modify it with SET when we're done
  // TODO: consolidate all these
  set_piece_at(&tmp, index, player);

  // we moved, so set state.consecutive_passes to 0
  tmp.consecutive_passes = 0;
  // copy state back to original object
  SET(*state, tmp);
  // update score with # of pieces flipped
  // TODO: should really decrement other player's scores, but oh well
  ai_add_player_score(player, count*100);

  // next player (if in search mode)
  if (ai_next_player())
  {
    play_turn(state);
  }
  return 1;
}

// bitmasks for edges of board and diagonals
const BoardMask LEFT   = LINE8(0,0,0,1);
const BoardMask RIGHT  = LINE8(7,0,0,1);
const BoardMask TOP    = LINE8(0,0,1,0);
const BoardMask BOTTOM = LINE8(0,7,1,0);
const BoardMask DIAGSE = LINE8(0,0,1,1);
const BoardMask DIAGNE = LINE8(0,7,1,-1);

// return bitmask of candidate moves (still must be validated)
// these are empty squares adjacent to enemy squares
BoardMask get_possible_moves(const GameState* state, int player)
{
  BoardMask occup = get_occupancy(state);
  BoardMask moves = ~occup; // empty squares
  BoardMask opp = occup ^ state->pieces[player]; // any opponent's squares
  // expand opponent's territory one square in all 8 directions
  BoardMask adj = opp;
  adj |= (~LEFT & opp) >> 1;
  adj |= (~RIGHT & opp) << 1;
  adj |= ((~TOP & adj) >> 8) | ((~BOTTOM & adj) << 8);
  // candidate moves are intersection of empty squares with expanded opponent's territory
  moves &= adj;
  //printf("p%d %llx %llx %llx -> %llx\n", player, occup, opp, adj, moves);
  return moves;
}

// verify each move by looking in all 8 directions for flippable pieces
BoardMask get_valid_moves(const GameState* state, int player)
{
  BoardMask ours = state->pieces[player];
  BoardMask mask = get_possible_moves(state, player);
  BoardMask result = mask;
  int i = 0;
  while (mask)
  {
    if (mask & 1)
    {
      int x = (i % BOARDX);
      int y = (i / BOARDX);
      // check bitmasks for early rejection of move
      // we look along lines in 8 directions for one of our pieces
      // if none exist, move is invalid
      BoardMask diag1 = (x < y) ? (DIAGSE << BI(0,y-x)) : (DIAGSE >> BI(0,x-y)); // down/up
      BoardMask diag2 = (x < 7-y) ? (DIAGNE >> BI(0,7-y-x)) : (DIAGNE << BI(0,x-7+y)); // up/down
      BoardMask test = (LEFT << BI(x,0)) | (TOP << BI(0,y)) | diag1 | diag2;
      //printf("test position %d,%d: %llx %llx -> %llx\n", x, y, ours, test, test&ours);
#ifdef ONLY_VALID_MOVES
      // move may be valid, let's test it
      if ((test & ours) != 0)
      {
        // count flippable pieces along all directions
        for (int dy=-1; dy<=1; dy++)
        {
          for (int dx=-1; dx<=1; dx++)
          {
            if (dx==0 && dy==0)
              continue;
            int n = count_flippable_pieces(state, player, x, y, dx, dy);
            if (n > 0)
            {
              assert((test & ours) != 0);
              goto valid;
            }
          }
        }
      }
      // move is invalid, unset
#else
      if ((test & ours) == 0)
#endif
        result &= ~(((BoardMask)1) << i);
    }
  valid:
    mask >>= 1;
    i++;
  }
  return result;
}

void play_turn(const GameState* state)
{
  BoardMask mask = get_valid_moves(state, ai_current_player());
  if (mask != 0)
  {
    // try all moves, are any valid?
    if (ai_choice(state, 0, make_move, 0, mask) == 0)
      mask = 0;
  }
  // none are valid, so player passes
  if (mask == 0)
  {
    if (player_passes(state))
      return; // game over
    else if (ai_next_player())
      play_turn(state); // if in search mode
  }
}

void play_game(const GameState* state)
{
  while (state->consecutive_passes < num_players)
  {
    print_board(state);
    play_turn(state);
  }
}

int main(int argc, char** argv)
{
  GameState state;
  
  ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 15;
  ai_init(&defaults);

  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
