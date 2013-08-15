
#include "ai.h"

// a "point" can only be owned by 1 player at a time
typedef struct
{
  unsigned int count:6;
  unsigned int occupied:2; // 0 = noone, 1 = p1, 2 = p2
} Point;

// game state
typedef struct 
{
  Point points[24]; // 24 points on the board (player 0 starts @ 0, player 1 @ 23)
  uint8_t bar[2]; // # of checkers on the bar for each player
  uint8_t total[2]; // # of checkers owned by each player

  int d1,d2; // last die rolls  
  int point_to_move;
} GameState;

#define PT(player,index) ((player)==0 ? (index) : 23-(index))

//

const Point MAKEPOINT(int player, int count)
{
  Point p;
  p.occupied = count ? player+1 : 0;
  p.count = count;
  return p;
}

// setup default game state and initial board
void init_game(GameState* state)
{
  memset(state, 0, sizeof(GameState));
  state->points[PT(0,23)] = MAKEPOINT(0,2);
  state->points[PT(0,7)]  = MAKEPOINT(0,3);
  state->points[PT(0,12)] = MAKEPOINT(0,5);
  state->points[PT(0,5)]  = MAKEPOINT(0,5);
  state->points[PT(1,23)] = MAKEPOINT(1,2);
  state->points[PT(1,7)]  = MAKEPOINT(1,3);
  state->points[PT(1,12)] = MAKEPOINT(1,5);
  state->points[PT(1,5)]  = MAKEPOINT(1,5);
  state->total[0] = 15;
  state->total[1] = 15;
}

void print_board(const GameState* state)
{
  int y,x;
  static const char* CH = ".XO";
  printf("\nBOARD (Player %d):\n", ai_current_player());
  for (int i=0; i<24; i++)
  {
    printf(" %2d %2d  ", 23-i, i);
    for (int j=0; j<state->points[i].count; j++)
      printf("%c", CH[state->points[i].occupied]);
    printf("\n");
  }
  printf("\n");
  for (int p=0; p<2; p++)
  {
    printf("P%d: %d total, bar %d", p, state->total[p], state->bar[p]);
    //for (int j=0; j<state->bar[p]; j++)
      //printf("%c", CH[p]);
    printf("\n");
  }
  printf("\n");
}

int is_game_over(const GameState* state)
{
  if (state->total[0] == 0)
    return 1;
  else if (state->total[1] == 0)
    return 2;
  else
    return 0;
}

void play_turn(const GameState* state);

typedef enum
{
  MOVE_INVALID,
  MOVE_BEAROFF,
  MOVE_UNOCCUPIED,
  MOVE_MERGE,
  MOVE_HITBLOT,
} MoveResult;

MoveResult can_move_to(const GameState* state, int player, int index)
{
  assert(index>=0 && index<24);
  int i = PT(player,index);
  if (state->points[i].occupied == 0)
    return MOVE_UNOCCUPIED;
  else if (state->points[i].occupied == player+1)
    return MOVE_MERGE;
  else if (state->points[i].count == 1)
    return MOVE_HITBLOT;
  else
    return 0;
}

MoveResult is_valid_move(const GameState* state, int player, int index, int d)
{
  assert(index>=0 && index<24);
  assert(state->points[PT(player,index)].occupied == player+1);
  
  if (d == 0 || index-d < -1)
    return 0; // out of bounds
  else if (index-d == -1)
    return MOVE_BEAROFF;

  return can_move_to(state, player, index-d);
}

int make_move(const GameState* state, int i, int d)
{
  assert(d > 0);
  int player = ai_current_player();
  MoveResult move = is_valid_move(state, player, i, d);
  DEBUG("make_move(): P%d moves %d -> %d (result %d)\n", player, i, i-d, move);
  int src = PT(player,i);
  int dest = PT(player,i-d);
  switch (move)
  {
    case MOVE_INVALID:
      return 0;
    case MOVE_BEAROFF:
      assert(src >= 0 && src < 24);
      SET(state->points[src], MAKEPOINT(player, state->points[src].count-1));
      DEC(state->total[player]);
      ai_set_player_score(player, 15 - state->total[player]);
      DEBUG("make_move(): P%d beared off @ %d; %d left\n", player, i, state->total[player]);
      return 1;
    case MOVE_UNOCCUPIED:
    case MOVE_MERGE:
      assert(src >= 0 && src < 24);
      assert(dest >= 0 && dest < 24);
      SET(state->points[src], MAKEPOINT(player, state->points[src].count-1));
      SET(state->points[dest], MAKEPOINT(player, state->points[dest].count+1));
      return 1;
    case MOVE_HITBLOT:
      assert(src >= 0 && src < 24);
      assert(dest >= 0 && dest < 24);
      SET(state->points[src], MAKEPOINT(player, state->points[src].count-1));
      SET(state->points[dest], MAKEPOINT(player, 1));
      INC(state->bar[player^1]);
      return 1;
  }
}

int make_move_0(const GameState* state);

int make_move_2(const void* pstate, ChoiceIndex index)
{
  const GameState* state = pstate;
  int d1 = state->d1;
  int d2 = state->d2;
  if (index == 3)
    SWAP(d1,d2);
  // make moves, either one at a time or together
  int pt = state->point_to_move;
  //print_board(state); printf("0 %d %d %d\n", ai_current_player(), index, pt);
  if (index != 1)
  {
    int m1 = make_move(state, pt, d1);
    // return failure if move is invalid
    // or if we bearoff but have a move remaining (TODO: prune earlier)
    if (m1 == MOVE_INVALID)
      return 0;
    if (m1 == MOVE_BEAROFF && index != 0)
      return 0;
    pt -= d1;
  }
  //print_board(state); printf("1 %d %d %d\n", ai_current_player(), index, pt);
  if (index != 0)
  {
    if (!make_move(state, pt, d2))
      return 0;
  }
  // did we play both our dice?
  if (index >= 2 || d1 == 0 || d2 == 0)
  {
    if (ai_next_player())
      play_turn(state); // if in search mode
    return 1;
  }
  // make another move
  if (index == 0)
  {
    SET(state->d1, 0);
  }
  else
  {
    SET(state->d2, 0);
  }
  return make_move_0(state);
}

int make_move_1(const void* pstate, ChoiceIndex index)
{
  const GameState* state = pstate;
  assert(index>=0 && index<24);
  SET(state->point_to_move, index);
  // 0 = move d1, then another
  // 1 = move d2, then another
  // 2 = move d1+d2
  // 3 = move d2+d1
  ChoiceMask mask;
  if (state->d1==0)
    mask = 2;
  else if (state->d2==0)
    mask = 1;
  else
    mask = 1+2+4+8;
    
  return ai_choice(state, 0, make_move_2, 0, mask);
}

int reenter_from_bar(const GameState* state, int player, int d)
{
  if (d)
  {
    assert(d>=1&&d<=6);
    if (state->bar[player])
    {
      MoveResult move = can_move_to(state, player, 24-d);
      if (move)
      {
        DEBUG("P%d entering from bar @ %d\n", player, 24-d);
        DEC(state->bar[player]);
        int dest = PT(player,24-d);
        switch (move)
        {
          case MOVE_UNOCCUPIED:
          case MOVE_MERGE:
            SET(state->points[dest], MAKEPOINT(player, state->points[dest].count+1));
            return 1;
          case MOVE_HITBLOT:
            SET(state->points[dest], MAKEPOINT(player, 1));
            INC(state->bar[player^1]);
            return 1;
          default:
            assert(0);
        }
        return 1;
      }
    }
  }
  return 0;
}

int make_move_0(const GameState* state)
{
  int d1 = state->d1;
  int d2 = state->d2;
  int player = ai_current_player();
  DEBUG("P%d dice %d %d\n", player, d1, d2);
  ChoiceMask mask = 0;
  // entering from bar has priority
  if (state->bar[player])
  {
    if (reenter_from_bar(state, player, d1))
    {
      SET(state->d1, 0);
      d1 = 0;
    }
    if (reenter_from_bar(state, player, d2))
    {
      SET(state->d2, 0);
      d2 = 0;
    }
  }
  // player can move any of 24 points
  // TODO: bearing off rules
  for (int i=0; i<24; i++)
  {
    // does this point have a valid move?
    if (state->points[PT(player,i)].occupied == player+1 &&
        (is_valid_move(state,player,i,d1) || is_valid_move(state,player,i,d2) || is_valid_move(state,player,i,d1+d2)))
    {
      mask |= (1<<i);
    }
  }
  if (mask != 0)
  {
    // try each of our pieces
    if (ai_choice(state, 0, make_move_1, 0, mask) == 0)
      mask = 0;
  }
  // none are valid, so player passes
  if (mask == 0)
  {
    DEBUG("P%d no moves\n", player);
    if (ai_next_player())
      play_turn(state); // if in search mode
  }
  return 1;
}

int roll_dice(const void* pstate, ChoiceIndex index)
{
  // TODO: doubles
  // TODO: compulsory move and bearing off (http://en.wikipedia.org/wiki/Backgammon#Movement)
  const GameState* state = pstate;
  int player = ai_current_player();
  // extract die rolls
  int d1 = (index % 6) + 1;
  int d2 = (index / 6) + 1;
  if (d1 > d2)
    SWAP(d1,d2);
  // TODO: make faster
  SET(state->d1, d1);
  SET(state->d2, d2);
  return make_move_0(state);
}

void play_turn(const GameState* state)
{
  int winner = is_game_over(state);
  if (winner)
  {
    // player won
    DEBUG("Player %d won\n", winner-1);
    ai_game_over();
  } else {
    // roll dice (2d6 -> 0-35)
    ai_chance(state, 0, roll_dice, 0, 36);
  }
}

void play_game(const GameState* state)
{
  while (!is_game_over(state))
  {
    print_board(state);
    /*
    // AI solve for this player?
    if (ai_set_mode_search())
    {
      play_turn(state);
      ai_print_stats();
    }
    // commit the best move, or prompt human for move
    if (!ai_set_mode_play())
      break;
    */
    play_turn(state);
  }
}

int main(int argc, char** argv)
{
  GameState state;
  
  ai_process_args(argc,argv);
  
  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 14;
  ai_init(&defaults);

  assert(num_players == 2);
  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
