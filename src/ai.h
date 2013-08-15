
#ifndef _AI_H
#define _AI_H

#include <memory.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

#include "hash.h"
#include "util.h"
#include "journal.h"

extern int search_level;

extern int num_players;

typedef struct 
{
  int num_players;
  int hash_table_order;
  int max_search_level;
  int max_walk_level;
} AIEngineParams;

#define MAX_PLAYERS 4
#define MAX_SCORE 1000000
#define MIN_SCORE -MAX_SCORE

typedef enum
{
  AI_UNKNOWN,
  AI_INTERACTIVE,
  AI_PLAY,
  // all search types start here
  AI_SEARCH,
  AI_RANDOM,
} AIMode;

// TODO: probably want to represent score here too
typedef enum 
{
  NO_VALID_MOVES = -2,
  INVALID_MOVE = -1,
  GAME_OVER = 0,
  GAME_DRAW = 0,
  PLAYER_1_WON = 1,
  PLAYER_2_WON = 2,
  PLAYER_3_WON = 3,
  PLAYER_4_WON = 4,
} ChoiceResult;

#define MAX_CUSTOM_STATS 16

typedef struct {
    ChoiceMask best_choices;
} SearchHeuristics;

typedef struct {
  SearchHeuristics heuristics; // must be first in struct
  uint64_t visits;
  uint64_t choices;
  uint64_t revisits;
  uint64_t cutoffs;
  uint64_t early_cutoffs;
  uint64_t advantage[MAX_PLAYERS];
  uint64_t wins[MAX_PLAYERS];
  uint64_t draws;
  int max_alpha;
  int min_beta;
  uint64_t custom[MAX_CUSTOM_STATS];
} SearchStats;

typedef int (*ChoiceFunction)(const void* state, ChoiceIndex index);

typedef int (*PlayerInteractionFunction)(const void* state, int player, ChoiceFunction choicefunc);

typedef struct PlayerSettings
{
  PlayerInteractionFunction pifunc;
  int max_search_depth;
} PlayerSettings;

#define AI_OPTION_CHANCE	1

//

void ai_init(const AIEngineParams* params);

PlayerSettings* ai_player_settings(int player);

int ai_current_player();

int ai_seeking_player();

bool ai_set_current_player(int player);

bool ai_next_player();

int ai_choice(const void* state, int state_size, ChoiceFunction move, int rangestart, ChoiceMask rangeflags);

#define ai_choice(a,b,c,d,e) ai_choice_ex(a,b,c,d,e,0,NULL)

typedef struct 
{
  float* probabilities;
} ChoiceParams;

int ai_choice_ex(const void* state, int state_size, ChoiceFunction move, int rangestart, ChoiceMask rangeflags,
  int optionflags, const ChoiceParams* params);

int ai_chance(const void* state, int state_size, ChoiceFunction move, int rangestart, int rangecount);

void ai_journal(const void* base, const void* dst, const void* src, unsigned int size);

void ai_add_player_score(int player, int addscore);

void ai_set_player_score(int player, int score);

int ai_get_player_score(int player);

int ai_get_winning_players();

void ai_game_over();

int ai_process_args(int argc, char** argv);

void ai_print_stats();

void ai_print_endgame_results();

AIMode ai_get_mode();

bool ai_is_searching();

HashCode ai_current_hash();

//

#endif /* _AI_H */
