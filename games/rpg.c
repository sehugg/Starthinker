
#include "ai.h"

#define BOARDX 32
#define BOARDY 32
#define MAXUNITS 128

typedef enum {
  GOOD,
  EVIL,
} TeamType;

#define UASLEEP 0x40
#define UAWAKE  0x80

// 0x00-0x3f = terrain
// 0x40-0x7f = sleeping units
// 0x80-0xff = active units
typedef enum {
  EMPTY,
  WALL,
  WINDOW,
  HEALTH,
  BEACON,
  KEY1,
  KEY2,
  KEY3,
  DOOR1,
  DOOR2,
  DOOR3,
} CellType;

typedef enum {
  BH_RANDOM,
  BH_CHASE,
} BehaviorType;

typedef enum {
  ACT_MELEE,
  ACT_MISSILE,
  ACT_MAGIC,
  ACT_HEAL,
  //
  ACT_MOVE,
  NUM_ACTIONS
} ActionType;

#define NUM_WEAPONS 4

typedef struct { 
  // really is a CellType, but signedness is undefined
  unsigned char type:8;
} __attribute__((packed)) Cell;

typedef struct {
  const char* name;
  int8_t damage;			// hit points each attack (0 = walk)
  uint8_t accuracy;			// 0-255 = 0%-100% probability
  uint8_t range;			// # of cells distance
  uint8_t recharge;			// # of turns to recharge
  uint8_t rest_turns;			// unit must remain stationary before/after using ability
} WeaponDef;

typedef struct {
  const char* name;
  uint8_t maxhp;
  uint8_t speed;
  uint8_t sight;			// # of squares can see
  WeaponDef weapons[NUM_WEAPONS];
} UnitDef;

typedef struct {
  int8_t x,y;
} XYOffset;

typedef struct {
  XYOffset pos;
  uint8_t hp;
  uint8_t type;
  uint8_t team;
  uint8_t activity;			// ongoing acivity (0 = none, 1-4 = weapons)
  int16_t lastusemod;			// turn # of last ability use
} Unit;

#define SPEED_ONE 16

typedef struct {
  Cell board[BOARDY][BOARDX];
  Unit units[MAXUNITS];
  int nunits;
  int turn;
  int scale;
} GameState;

#define MAX_RANGE 5
#define MAX_DIROFS (MAX_RANGE*(MAX_RANGE-1))*4

XYOffset DIROFS[MAX_DIROFS];
int DIRINDEX[MAX_RANGE+1];

static XYOffset MAKEXY(x,y)
{
  XYOffset xy = { x,y };
  return xy;
}

typedef struct {
  XYOffset srcpos;
  Unit* srcunit;
  XYOffset dstpos;
  ActionType action;
} Move;

static UnitDef UNIT_DEFS[] = {
  { "Player", 20, SPEED_ONE*2, 5, { "Fisticuffs", 2, 255, 2, 1, 0, 0 } },
  { "Badguy", 5,  SPEED_ONE, 3, { "Claws", 1, 255, 1, 1, 0, 0 } },
};

void print_board(const GameState* state);

static Cell EMPTY_CELL = { EMPTY };
static Unit EMPTY_UNIT = { };

#define SETCELLEMPTY(pcell) SET(pcell->type,EMPTY)
#define UNITIDX(punit) ((punit - &state->units[0]) / sizeof(Unit))

bool is_valid_space(int x, int y)
{
  return ((unsigned int)x < BOARDX && (unsigned int)y < BOARDY);
}

bool unit_can_move(const GameState* state, const Unit* unit)
{
  return true; // TODO
}

const Unit* wakeup_unit(const GameState* state, const Cell* cell, int type)
{
  int ofs = (cell - &state->board[0][0]) / sizeof(Cell);
  Unit unit = {};
  unit.pos.x = ofs % BOARDX;
  unit.pos.y = ofs / BOARDX;
  unit.type = type;
  unit.team = EVIL; // TODO?
  unit.hp = UNIT_DEFS[type].maxhp;
  // add new unit
  const Unit* dstunit = &state->units[state->nunits];
  Cell newcell = { state->nunits + UAWAKE };
  SET(*cell, newcell);
  SET(*dstunit, unit);
  INC(state->nunits);
  return dstunit;
}

const Unit* cell2unit(const GameState* state, const Cell* cell)
{
  if (cell->type >= UAWAKE)
    return &state->units[cell->type - UAWAKE]; // awake
  else if (cell->type >= UASLEEP)
    return wakeup_unit(state, cell, cell->type - UASLEEP); // asleep
  else
    return NULL; // no unit, just terrain
}

void kill_unit(const GameState* state, const Unit* unit)
{
  SET(state->board[unit->pos.y][unit->pos.x], EMPTY_CELL);
  SET(*unit, EMPTY_UNIT);
  // TODO: garbage collect
}

void add_damage(const GameState* state, const Unit* unit, int damage)
{
  int newhp = unit->hp - damage;
  if (newhp > 0)
  {
    ai_add_player_score(ai_current_player(), damage*100);
    SET(unit->hp, newhp);
    DEBUG("%s took %d damage, hp = %d\n", UNIT_DEFS[unit->type].name, damage, unit->hp);
  }
  else
  {
    ai_add_player_score(ai_current_player(), unit->hp*100);
    DEBUG("%s took %d damage, killed\n", UNIT_DEFS[unit->type].name, damage);
    kill_unit(state, unit);
  }
}

bool commit_move(const GameState* state, const Move* move)
{
  assert(is_valid_space(move->srcpos.x, move->srcpos.y));
  assert(is_valid_space(move->dstpos.x, move->dstpos.y));
  
  const Cell* srccell = &state->board[move->srcpos.y][move->srcpos.x];
  const Unit* srcunit = cell2unit(state,srccell);
  DEBUG("srcpos = %d,%d / %d\n", move->srcpos.x, move->srcpos.y, srccell->type);
  assert(move->srcunit != NULL);
  assert(srcunit != NULL);

  const Cell* dstcell = &state->board[move->dstpos.y][move->dstpos.x];
  const Unit* dstunit = cell2unit(state,dstcell);
  
  // move?
  if (move->action == ACT_MOVE)
  {
    // already occupied? can't go
    assert(dstunit == NULL);
    DEBUG("commit_move(%d,%d) -> (%d,%d)\n", move->srcpos.x, move->srcpos.y, move->dstpos.x, move->dstpos.y);
    
    SET(srcunit->pos, move->dstpos);
    SET(*dstcell, *srccell);
    SET(*srccell, EMPTY_CELL);
    return true;
  }
  // can we attack?
  else
  {
    assert(dstunit != NULL);
    // TODO: assert(srcunit->lastusemod)
    const UnitDef* srcdef = &UNIT_DEFS[srcunit->type];
    const WeaponDef* weapon = &srcdef->weapons[move->action];
    DEBUG("commit_move(%d,%d) -> (%d,%d) with %s\n", move->srcpos.x, move->srcpos.y, move->dstpos.x, move->dstpos.y, weapon->name);
    // on the wrong team?
    assert( (srcunit->team == dstunit->team) ^ (weapon->damage >= 0) );
    // TODO: blocked?
    // TODO: did we connect?
    // inflict damage
    add_damage(state, dstunit, weapon->damage * state->scale);
    // set last used
    SET(srcunit->lastusemod, state->turn);
    return true;
  }
}

//

const char CHAR_PER_TERRAIN[0x40]  = ".#";
const char CHAR_PER_UNITTYPE[0x40] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

#define COLOR_RED "\033[31m"
#define COLOR_BLUE "\033[34m"
#define COLOR_RED_VIS "\033[31;1m"
#define COLOR_BLUE_VIS "\033[34;1m"
#define COLOR_NML "\033[0m"

void print_board(const GameState* state)
{
  printf("\n");
  int x,y;
  for (y=0; y<BOARDY; y++)
  {
    printf("    ");
    for (x=0; x<BOARDX; x++)
    {
      char ch;
      const Cell* cell = &state->board[y][x];
      if (cell->type < UASLEEP)
        ch = CHAR_PER_TERRAIN[cell->type];
      else if (cell->type < UAWAKE)
        ch = CHAR_PER_UNITTYPE[cell->type - UAWAKE];
      else
      {
        const Unit* unit = &state->units[cell->type - UAWAKE];
        ch = CHAR_PER_UNITTYPE[unit->type];
        printf(unit->team ? COLOR_RED_VIS : COLOR_BLUE_VIS);
      }
      printf("%c", ch);
      printf(COLOR_NML);
    }
    printf("\n");
  }
  printf("\n");
}

//

void init_game(GameState* state)
{
#define rnd(n) (((unsigned int)random()) % n)

  memset(state, 0, sizeof(GameState));
  state->scale = 1;
  // make walls
  for (int i=0; i<110; i++)
  {
    int x = rnd(BOARDX);
    int y = rnd(BOARDY);
    state->board[y][x].type = WALL;
  }
  // make player
  {
    int py = BOARDY/4;
    int px = BOARDX/2;
    Unit unit = {};
    unit.pos.x = px;
    unit.pos.y = py;
    unit.type = 0;
    unit.team = GOOD;
    unit.hp = 20; // TODO: make_unit()
    state->board[py][px].type = UAWAKE + 0;
    state->units[state->nunits++] = unit;
  }
  // make enemies
  for (int i=0; i<10; i++)
  {
    int ex = rnd(BOARDX);
    int ey = rnd(BOARDY);
    int type = 1;
    state->board[ey][ex].type = UASLEEP + type;
    // TODO: sleep mode
    cell2unit(state, &state->board[ey][ex]);
  }
}

/*
1. Choose unit to move
2. Choose action to take (weapon, or move)
3. Choose target of action
*/

int play_turn(const GameState* state);

static Move current_move;

int make_move(const void* pstate, ChoiceIndex destindex)
{
  int player = ai_current_player();
  const GameState* state = pstate;

  assert(destindex < MAX_DIROFS);
  XYOffset xy = DIROFS[destindex];
  xy.x = xy.x*state->scale + current_move.srcpos.x;
  xy.y = xy.y*state->scale + current_move.srcpos.y;
  SETGLOBAL(current_move.dstpos, xy);

  if (!commit_move(state, &current_move))
    return 0;
  
  // next player
  if (ai_next_player())
  {
    play_turn(state);
  }
  return 1;
}

int choose_destination(const void* pstate, ChoiceIndex actindex)
{
  assert(actindex < NUM_ACTIONS);
  SETGLOBAL(current_move.action, actindex);
  
  const GameState* state = pstate;
  
  // how many squares?
  const UnitDef* srcdef = &UNIT_DEFS[current_move.srcunit->type];
  int range = (actindex < NUM_WEAPONS) ? srcdef->weapons[actindex].range : srcdef->speed/SPEED_ONE; // TODO: multiple
  assert(range <= MAX_RANGE);
  int numsquares = DIRINDEX[range];
  DEBUG("Checking %d squares for r=%d\n", numsquares, range);
  
  ChoiceMask destmask = 0;
  for (int i=0; i<numsquares; i++)
  {
    const XYOffset xy = DIROFS[i];
    int x = current_move.srcpos.x + xy.x * state->scale;
    int y = current_move.srcpos.y + xy.y * state->scale;
    if (is_valid_space(x,y))
    {
      const Cell* cell = &state->board[y][x];
      // move to empty spaces
      if (actindex == ACT_MOVE)
      {
        if (cell->type == EMPTY)
        {
          DEBUG("Checking move to %d,%d (type 0x%x)\n", x, y, cell->type);
          destmask |= CHOICE(i);
        }
      }
      // use a weapon; is there a unit there? (asleep or awake)
      else if (cell->type >= UASLEEP) 
      {
        const Unit* unit = cell2unit(state, cell);
        if (unit != NULL && ((unit->team != current_move.srcunit->team) ^ (actindex == ACT_HEAL)))
        {
          DEBUG("Checking attack on %s @ %d,%d\n", UNIT_DEFS[unit->type].name, x, y);
          destmask |= CHOICE(i);
        }
      }
    }
  }

  return destmask ? ai_choice(state, 0, make_move, 0, destmask) : 0;
}

int choose_action(const void* pstate, ChoiceIndex unitindex)
{
  int player = ai_current_player();
  const GameState* state = pstate;
  assert(unitindex < state->nunits);
  const Unit* unit = &state->units[unitindex];
  const UnitDef* def = &UNIT_DEFS[unit->type];
  
  SETGLOBAL(current_move.srcpos, unit->pos);
  SETGLOBAL(current_move.srcunit, (Unit*)unit);
  DEBUG("srcpos = %d,%d / %d %d\n", unit->pos.x, unit->pos.y, unit->type, unitindex);
  assert(unit != NULL);
  assert(state->board[unit->pos.y][unit->pos.x].type == unitindex + UAWAKE);
  
  int actionmask = 0;
  if (def->speed)
    actionmask |= CHOICE(ACT_MOVE);
  for (int i=ACT_MELEE; i<=ACT_HEAL; i++)
  {
    if (def->weapons[i].damage)
      actionmask |= CHOICE(i);
  }
  
  return ai_choice(state, 0, choose_destination, 0, actionmask);
}

int play_turn(const GameState* state)
{
  INC(state->turn);
  // scale at high search levels
  if (ai_is_searching() && (search_level % (3*4)) == 0)
  {
    SET(state->scale, search_level / (3*4) + 1);
    DEBUG("Setting scale = %d\n", state->scale);
  }
  ChoiceMask srcunitmask = 0;
  int player = ai_current_player();
  for (int i=0; i<state->nunits; i++)
  {
    const Unit* unit = &state->units[i];
    if (unit->hp && unit->team == player && unit_can_move(state,unit))
    {
      //DEBUG("Considering unit @ %d,%d", unit->pos.x, unit->pos.y);
      srcunitmask |= CHOICE(i);
    }
  }
  if (srcunitmask || player!=GOOD)
  {
    return ai_choice(state, 0, choose_action, 0, srcunitmask);
  }
  else
  {
    ai_set_player_score(player, 0);
    return false;
  }
}

void play_game(GameState* state)
{
  while (true) // !is_game_over(state))
  {
    int player = ai_current_player();
    printf("\nTURN %d PLAYER: %d (score %+d)\n", state->turn, player, ai_get_player_score(player));
    print_board(state);
    if (!play_turn(state))
    {
      printf("\n*** NO MORE MOVES\n");
      return;
    }
  }
  
  printf(COLOR_RED "\n***GAME OVER: Player(s) 0x%x won\n" COLOR_NML, ai_get_winning_players());
  print_board(state);
}

void init_tables()
{
  int i=0;
  DIRINDEX[0] = 0;
  for (int r=1; r<=MAX_RANGE; r++)
  {
    for (int j=0; j<r; j++)
      DIROFS[i++] = MAKEXY(r-j,j); // 2,0
    for (int j=0; j<r; j++)
      DIROFS[i++] = MAKEXY(-j,r-j); // 0,2
    for (int j=0; j<r; j++)
      DIROFS[i++] = MAKEXY(j-r,-j); // -2,0
    for (int j=0; j<r; j++)
      DIROFS[i++] = MAKEXY(j,j-r); // 0,-2
    DIRINDEX[r] = i;
    //printf("%d; %d\n", r, i);
  }
}

int main(int argc, char** argv)
{
  assert(sizeof(Cell)==1); // make sure it's packed properly
  GameState state;

  ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 3*6;
  defaults.max_walk_level = 50; // TODO
  ai_init(&defaults);

  init_tables();
  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
