
#include "ai.h"

// game state
typedef struct 
{
  int turn_total[MAX_PLAYERS];
} GameState;

void init_game(GameState* state)
{
  memset(state, 0, sizeof(GameState));
}

void print_board(const GameState* state)
{
  printf("\n");
  for (int p=0; p<num_players; p++)
  {
    printf("Player %d: score = %d (%d this turn)\n", p, ai_get_player_score(p), state->turn_total[p]);
  }
  printf("\n");
}

void play_turn(const GameState* state);

int make_move(const void* pstate, ChoiceIndex hold)
{
  const GameState* state = pstate;
  int player = ai_current_player();

  switch (hold)
  {
    // hold and pass
    case 0:  
      DEBUG("P%d: Hold %+d points\n", player, state->turn_total[player]);
      ai_add_player_score(player, state->turn_total[player]);
      SET(state->turn_total[player], 0);
      if (ai_next_player())
      {
        play_turn(state);
      }
      return 1;

    // roll again      
    case 1:
      play_turn(state);
      return 1;
      
    default:
      assert(0);
      return 0;
  }
}

int die_rolled(const void* pstate, ChoiceIndex die)
{
  const GameState* state = pstate;
  int player = ai_current_player();
  DEBUG("P%d: Roll %d\n", player, die);
  
  switch (die)
  {
    case 0:
      // forfeit turn
      SET(state->turn_total[player], 0);
      if (ai_next_player())
      {
        play_turn(state);
      }
      return 1;
    
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
      // add die roll to turn total
      SET(state->turn_total[player], state->turn_total[player] + die + 1);
      DEBUG("P%d: Added %+d points -> %d\n", player, die+1, state->turn_total[player]);
      // roll or hold?
      ai_choice(state, 0, make_move, 0, 0x3);
      return 1;
      
    default:
      assert(0);
      return 0;
  }
}

int is_game_over(const GameState* state)
{
  for (int i=0; i<num_players; i++)
  {
    if (ai_get_player_score(i) >= 100)
      return i+1;
  }
  return 0;
}

void play_turn(const GameState* state)
{
  int winner = is_game_over(state);
  if (winner)
  {
    // player won
    ai_game_over();
  } else {
    // roll dice
    ai_chance(state, sizeof(GameState), die_rolled, 0, 6);
  }
}

void play_game(const GameState* state)
{
  while (!is_game_over(state))
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
  defaults.max_search_level = 9;
  ai_init(&defaults);

  assert(num_players == 2);
  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
