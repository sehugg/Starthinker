
#include "ai.h"

//#define USE_REVISITS

#define COLOR_RED "\033[31m"
#define COLOR_BLUE "\033[34m"
#define COLOR_RED_VIS "\033[31;1m"
#define COLOR_BLUE_VIS "\033[34;1m"
#define COLOR_NML "\033[0m"

typedef enum {
  None,
  Bomb,
  Flag,
  Spy,
  Scout,
  Miner,
  Sergeant,
  Lieutenant,
  Captain,
  Major,
  Colonel,
  General,
  Marshall,
  NUM_PIECE_TYPES
} PieceType;

static int PIECES_PER_TYPE[NUM_PIECE_TYPES] = { 0, 6, 1, 1, 8, 5, 4, 4, 4, 3, 2, 1, 1 };
static int MOVES_PER_TYPE[NUM_PIECE_TYPES] = { 0, 0, 0, 1, 10, 1, 1, 1, 1, 1, 1, 1, 1 };
static int RANKS_PER_TYPE[NUM_PIECE_TYPES] = { 0, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

static char* CHARS_PER_TYPE = ".BFS234567890";

typedef enum {
  RED,
  BLUE,
} PlayerType;

#define NPLAYERS 2

typedef enum {
  NORTH, EAST, SOUTH, WEST, NODIR
} Direction;

static char DIRX[] = { 0, 1, 0, -1, 0 };
static char DIRY[] = { -1, 0, 1, 0, 0 };

typedef struct {
  PieceType type:4;
  PlayerType player:1;
  bool moved:1;
  bool revealed:1;
} __attribute__((packed)) PieceDef;

#define XMAX 10
#define YMAX 10
#define YDEEP 4

typedef struct {
  unsigned char y,x;
} Position;

typedef struct {
  Position start;
  Position end;
} Move;

#ifdef USE_REVISITS
#define MAX_REVISITS 3
typedef uint8_t VisitsBoard[YMAX][XMAX];
static VisitsBoard BLANK_VISITS = {};
#endif

// Game state:
// - pieces on board
// - # of pieces of each type for each player
// - # of turns since capture
// - during search, # of pieces revealed of each type
// - (optional) board that counts # of times each square visited since last capture
typedef struct {
  PieceDef board[YMAX][XMAX];
  uint8_t npieces[NPLAYERS][NUM_PIECE_TYPES];
  uint8_t revealed[NPLAYERS][NUM_PIECE_TYPES];
  int turns_since_capture;
#ifdef USE_REVISITS
  VisitsBoard visits;
#endif
} GameState;

typedef enum {
  LOSE,
  DRAW,
  WIN,
} AttackResult;

typedef struct {
  float probs[3];
} AttackProbabilities;

typedef struct {
  int piece_values[NUM_PIECE_TYPES];
  int reveal_penalty_pct;
  int move_penalty;
} PlayerStrategy;

static PlayerStrategy DEFAULT_STRATEGY = {
  { 0, 75, 10000, 100, 10, 100, 20, 50, 100, 140, 175, 300, 400 },
  30,
  5,
};

PlayerStrategy player_strategies[2];

#define MAX_TURNS 1000

int num_turns = 0;

static PieceDef MAKEPIECE(PlayerType player, PieceType type)
{
  PieceDef def = { type, player };
  return def;
}

//

int get_piece_score(const PieceDef def, int xx, int yy)
{
  if (def.type == 0)
    return 0;

  // TODO: don't we want to use the seeking player's strategy?
  const PlayerStrategy* strategy = &player_strategies[def.player];
  // base value
  int n = strategy->piece_values[def.type];
  // lower score if revealed or moved
  if (def.revealed)
    n -= n * strategy->reveal_penalty_pct / 100;
  else if (def.moved)
    n -= strategy->move_penalty;
  return n;
}

bool remove_piece(const GameState* state, int x, int y, PieceType assumed_type)
{
  PieceDef old = state->board[y][x];
  if (old.type)
  {
    DEBUG("Removing piece @ %d,%d (type %c/%c)\n", x, y, CHARS_PER_TYPE[old.type], CHARS_PER_TYPE[assumed_type]);
    // if piece is hidden, we remove the assumed piece instead of the actual (hidden) piece
    if (assumed_type)
      old.type = assumed_type;
    // if we have no more pieces of this type, we fail
    if (state->npieces[old.player][old.type] == 0)
    {
      DEBUG("P%d ran out of pieces of type %c\n", old.player, CHARS_PER_TYPE[old.type]);
      assert(ai_is_searching());
      return false;
    }
    DEC(state->npieces[old.player][old.type]);
    if (old.revealed)
    {
      DEC(state->revealed[old.player][old.type]);
    }
    // TODO: score is not quite right
    ai_add_player_score(old.player, -get_piece_score(old,x,y));
    SET(state->board[y][x], MAKEPIECE(0, None));
  }
  return true;
}

void set_piece(const GameState* state, int x, int y, const PieceDef def)
{
  assert(state->board[y][x].type == 0);
  if (def.type)
  {
    DEBUG("Adding piece @ %d,%d (type %c)\n", x, y, CHARS_PER_TYPE[def.type]);
    SET(state->board[y][x], def);
    INC(state->npieces[def.player][def.type]);
    if (def.revealed)
    {
      INC(state->revealed[def.player][def.type]);
    }
    ai_add_player_score(def.player, get_piece_score(def,x,y));
#ifdef USE_REVISITS
    if (state->visits[y][x] < MAX_REVISITS)
    {
      INC(state->visits[y][x]);
    }
#endif
  }
}

void move_piece(const GameState* state, int x1, int y1, int x2, int y2, const PieceDef def)
{
  const PieceDef old = state->board[y1][x1];
  assert(old.type);
  assert(def.type == old.type);
  assert(state->board[y2][x2].type == 0);
  
  DEBUG("Moving piece @ %d,%d to %d,%d (type %c)\n", x1, y1, x2, y2, CHARS_PER_TYPE[def.type]);
  ai_add_player_score(def.player, get_piece_score(def,x2,y2) - get_piece_score(old,x1,y1));
  SET(state->board[y1][x1], MAKEPIECE(0, None));
  SET(state->board[y2][x2], def);
  if (!old.revealed && def.revealed)
  {
    INC(state->revealed[def.player][def.type]);
  }
#ifdef USE_REVISITS
    if (state->visits[y2][x2] < MAX_REVISITS)
    {
      INC(state->visits[y2][x2]);
    }
#endif
}

bool is_valid_space(int x, int y)
{
  if (x >= XMAX || x < 0 || y >= YMAX || y < 0)
    return 0;
  else
    return (y < YDEEP || y >= YMAX-YDEEP) || ((x&3)>>1)==0;
}

void init_game(GameState* state)
{
#define rnd(n) (((unsigned int)random()) % n)
  int player,type,i;
  memset(state, 0, sizeof(GameState));
  for (player=RED; player<=BLUE; player++)
  {
    for (type=0; type<NUM_PIECE_TYPES; type++)
    {
      int npieces = PIECES_PER_TYPE[type];
      for (i=0; i<npieces; i++)
      {
        PieceDef def = MAKEPIECE(player,type);
        int x,y;
        do {
          x = rnd(XMAX);
          y = rnd(YDEEP);
          if (player == BLUE) y = YMAX-1-y;
        } while (state->board[y][x].type != None);
        set_piece(state, x, y, def);
      }
    }
  }
}

void print_board(const GameState* state)
{
  printf("\n");
  int x,y;
  for (y=0; y<YMAX; y++)
  {
    printf("    ");
    for (x=0; x<XMAX; x++)
    {
      char ch;
      const PieceDef def = state->board[y][x];
      ch = is_valid_space(x,y) ? '.' : '#';
      if (def.type)
      {
        ch = CHARS_PER_TYPE[def.type];
        if (def.revealed) printf(def.player==RED ? COLOR_RED_VIS : COLOR_BLUE_VIS);
        else if (def.moved) printf(def.player==RED ? COLOR_RED : COLOR_BLUE);
      }
      printf("%c", ch);
      printf(COLOR_NML);
    }
#ifdef USE_REVISITS
    printf("        ");
    for (x=0; x<XMAX; x++)
    {
      int n = state->visits[y][x];
      printf("%c", n ? ('0'+n) : ' ');
    }
#endif
    printf("\n");
  }
  printf("\n");
}

int play_turn(const GameState* state);

AttackResult get_attack_result(PieceType a, PieceType d)
{
    if (a == Spy && d == Marshall)
      return WIN;
      
    if (a == Miner && d == Bomb)
      return WIN;
      
    int x = RANKS_PER_TYPE[a] - RANKS_PER_TYPE[d];
    if (x > 0)
      return WIN;
    else if (x < 0)
      return LOSE;
    else
      return DRAW;
}

// not used -- we consider all possible pieces, not just the 3 attack outcomes
AttackProbabilities get_possible_attack_results(const GameState* state, const PieceDef* attack, const PieceDef* defend)
{
  AttackProbabilities probs;
  probs.probs[WIN] = 0;
  probs.probs[LOSE] = 0;
  probs.probs[DRAW] = 0;
  
  // do we know what piece it is?
  if (defend->revealed)
  {
    probs.probs[get_attack_result(attack->type, defend->type)] = 1;
  }
  else 
  {
    int denom = 0;
    int type;
    for (type=1; type<NUM_PIECE_TYPES; type++)
    {
      // did this one move? if so don't consider Bomb or Flag
      if (defend->moved && MOVES_PER_TYPE[type] == 0)
        continue;
      
      // we know if the other player has exhausted all of one type and can exclude it
      int np = state->npieces[ai_current_player()^1][type];
      if (np>0)
      {
        probs.probs[get_attack_result(attack->type, type)] += np;
        denom += np;
      }
    }
    probs.probs[WIN] /= denom;
    probs.probs[LOSE] /= denom;
    probs.probs[DRAW] /= denom;
  }
  return probs;
}

// we use these to pass params between move functions
static int move_xpos = 0;
static int move_ypos = 0;
static Move next_move = {};

int commit_move(const GameState* state, const Move* move, PieceType assumed_type)
{
  int x1 = move->start.x;
  int y1 = move->start.y;
  int x2 = move->end.x;
  int y2 = move->end.y;
  assert(x1!=x2 || y1!=y2);
  DEBUG("Player %d move: %d %d -> %d %d\n", ai_current_player(), move->start.x, move->start.y, move->end.x, move->end.y);
  PieceDef piece = state->board[move->start.y][move->start.x];
  PieceDef piece2 = state->board[move->end.y][move->end.x];
  piece.moved = true;
  // did we reveal to be Scout?
  if (abs(x2-x1) + abs(y2-y1) > 1)
    piece.revealed = true;
  // for search mode, assume the defender to be a certain piece
  PieceType assumed_attacker = 0;
  PieceType assumed_defender = 0;
  if (assumed_type)
  {
    if (ai_seeking_player() == piece.player)
    {
      assert(state->npieces[piece2.player][assumed_type] > 0);
      piece2.type = assumed_defender = assumed_type;
      DEBUG("Player %d is defending, assumed %c\n", piece2.player, CHARS_PER_TYPE[assumed_type]);
    }
    else
    {
      assert(state->npieces[piece.player][assumed_type] > 0);
      piece.type = assumed_attacker = assumed_type;
      DEBUG("Player %d is attacking, assumed %c\n", piece2.player, CHARS_PER_TYPE[assumed_type]);
    }
  }

  // TODO: we still leak information via player score when we remove unrevealed piece
  if (piece2.type)
  {
    AttackResult result = get_attack_result(piece.type, piece2.type);
    piece.revealed = true;
    piece2.revealed = true;
    // we don't accurately count # of pieces in search mode due to assumptions
    // if we ran out of pieces of either type in search mode, fail
    if (!remove_piece(state, x1, y1, assumed_attacker) || !remove_piece(state, x2, y2, assumed_defender))
      return false;
      
    if (result==LOSE)
    {
      // if attacker loses, defender still reveals their piece
      set_piece(state, x2, y2, piece2);
    }
    else if (result==WIN)
    {
      // if attacker wins, replaces defender's position
      set_piece(state, x2, y2, piece);
    }
    else if (result==DRAW)
    {
      // if draw, both are removed
    }
    // reset turns since capture counter
    if (state->turns_since_capture)
    {
      // speed along the game:
      // if in search mode, give points to the attacker
      // proportional to number of turns that have passed w/o capture (whether or not they win)
      if (ai_is_searching() && state->turns_since_capture > 20)
      {
        ai_add_player_score(ai_current_player(), state->turns_since_capture * 100);
      }
      SET(state->turns_since_capture, 0);
    }
#ifdef USE_REVISITS
    // clear visited board
    ai_journal(state, state->visits, BLANK_VISITS, sizeof(VisitsBoard));
#endif
  } else {
    move_piece(state, x1, y1, x2, y2, piece);
  }
  return 1;
}

int attack_piece(const void* pstate, ChoiceIndex assumed_type)
{
  return commit_move((const GameState*)pstate, &next_move, assumed_type);
}

int eval_move(const GameState* state, int x1, int y1, const PieceDef attack, int x2, int y2, const PieceDef defend)
{
  Move move;
  move.start.x = x1;
  move.start.y = y1;
  move.end.x = x2;
  move.end.y = y2;
  
  // if attacking unrevealed piece in search mode,
  // evaluate defending piece across all (possible) types
  int seeker = ai_seeking_player();
  bool hidden_attacker = defend.type && defend.player == seeker && !attack.revealed;
  bool hidden_defender = defend.type && attack.player == seeker && !defend.revealed;
  assert(!(hidden_attacker && hidden_defender)); // can't have both attacker and defender hidden
  if ((hidden_attacker || hidden_defender) && ai_is_searching())
  {
    ChoiceParams params;
    float probs[NUM_PIECE_TYPES];
    params.probabilities = probs;
    PieceDef hidden = hidden_attacker ? attack : defend;
    int mask = 0;
    for (PieceType type=1; type<NUM_PIECE_TYPES; type++)
    {
      // did this one move? if so don't consider Bomb or Flag
      if (hidden.moved && MOVES_PER_TYPE[type] == 0)
        continue;
      
      // we know if the other player has exhausted all of one type and can exclude it
      int np = state->npieces[hidden.player][type];
      np -= state->revealed[hidden.player][type];
      if (np > 0)
      {
        mask |= (1 << type);
        probs[type] = np; // probability proportional to # of pieces of that type
      }
    }
    DEBUG("Piece @ %d,%d attacking %d,%d; %s mask = 0x%x\n", x1, y1, x2, y2, hidden_attacker?"attacker":"defender", mask);
    if (mask == 0)
      return 0;
    SETGLOBAL(next_move, move);
    // TODO: this could be better modeled as the opposing player's choice rather than mere chance
    return ai_choice_ex(state, 0, attack_piece, 0, mask, AI_OPTION_CHANCE, &params);
  }
  else
  {
    // either there is no defender or they are revealed
    // so just complete the move without modifying the defender's type
    return commit_move(state, &move, 0);
  }
}

bool can_move_to(const GameState* state, int x, int y, int player)
{
  return is_valid_space(x, y) &&
#ifdef USE_REVISITS
    state->visits[y][x] < MAX_REVISITS &&
#endif
    (state->board[y][x].type == 0 || state->board[y][x].player != player);
}

bool can_move(const GameState* state, int x, int y, int player)
{
  if (!is_valid_space(x,y))
    return false;

  const PieceDef def = state->board[y][x];
  return def.type && def.player == player && MOVES_PER_TYPE[def.type] &&
         (can_move_to(state, x-1, y, player) ||
          can_move_to(state, x+1, y, player) ||
          can_move_to(state, x, y-1, player) ||
          can_move_to(state, x, y+1, player));
}

int make_move_dir(const void* pstate, ChoiceIndex dir)
{
  if (dir == NODIR)
    goto nodir;
  const GameState* state = pstate;
  const int x1 = move_xpos;
  const int y1 = move_ypos;
  const PieceDef def = state->board[y1][x1];
  // only Scout can make more than +1 move
  int maxmoves = MOVES_PER_TYPE[def.type];
  assert(maxmoves); // can't be Bomb or Flag
  int x = x1;
  int y = y1;
  int player = def.player;
  int dd = dir;
  int dx = DIRX[dd];
  int dy = DIRY[dd];
  int i;
  x += dx;
  y += dy;
  if (!can_move_to(state,x,y,def.player))
   return 0; // can't go off board or thru lakes

  const PieceDef p = state->board[y][x];
  // is another piece there?
  if (p.type)
  {
    // can only attack (the other player) if adjacent (i.e. if Scout, our first square)
    if (/* i == 1 && */ p.player != def.player)
    {
      return eval_move(state, x1, y1, def, x, y, p);
    } else {
      return 0;
    }
  }
  else
  {
    // just a simple move, no attack
    if (!eval_move(state, x1, y1, def, x, y, p))
      return 0;
    // are we a Scout? (more than 1 move)
    // TODO: can't attack if > 1 move
    if (maxmoves > 1 && can_move_to(state, x, y, player))
    {
      // NODIR means don't go at all
      if (x != move_xpos) { SETGLOBAL(move_xpos, x); }
      if (y != move_ypos) { SETGLOBAL(move_ypos, y); }
      ai_choice(pstate, 0, make_move_dir, 0, (1<<dir) | (1<<NODIR));
    }
  }
nodir:
  // next player
  if (ai_next_player())
  {
    play_turn(state);
  }
  return 1;
}

int make_move_x(const void* pstate, ChoiceIndex x)
{
  SETGLOBAL(move_xpos, x);
  return ai_choice(pstate, 0, make_move_dir, 0, RANGE(0,3));
}

int make_move_y(const void* pstate, ChoiceIndex y)
{
  const GameState* state = pstate;
  // next, which columns in this row have our pieces?
  int player = ai_current_player();
  ChoiceMask mask = 0;
  int x;
  SETGLOBAL(move_ypos, y); // save for later
  for (x=0; x<XMAX; x++)
  {
    if (can_move(state, x, y, player))
    {
      mask |= (1<<x);
    }
  }
  // mask = bitmask of all columns in this row
  return ai_choice(state, 0, make_move_x, 0, mask);
}

int is_game_over(const GameState* state)
{
  // game is over when either player loses flag
  if (state->npieces[RED][Flag] == 0)
    return BLUE+1;
  else if (state->npieces[BLUE][Flag] == 0)
    return RED+1;
  else
    return 0;
}

int play_turn(const GameState* state)
{
  // game over?
  int winner = is_game_over(state);
  if (winner > 0)
  {
    DEBUG("Player %d won\n", winner-1);
    //ai_set_player_score(winner-1, MAX_SCORE);
    ai_game_over();
    return 0;
  }
  INC(state->turns_since_capture);
  // look at all moves for all pieces
  // first, which rows (Y) have our pieces?
  int player = ai_current_player();
  ChoiceMask mask = 0;
  int x,y;
  for (y=0; y<YMAX; y++)
  {
    for (x=0; x<XMAX; x++)
    {
      if (can_move(state, x, y, player))
      {
        mask |= (1<<y);
        break;
      }
    }
  }
  // mask = bitmask of all Y rows
  if (mask)
  {
    if (!ai_choice(state, 0, make_move_y, 0, mask))
      mask = 0;
  }
  // any valid moves?
  if (mask == 0)
  {
    // player cannot move, forfeit
    DEBUG("Player %d: no moves\n", player);
    ai_set_player_score(player, 0);
    return 0;
  } else
    return 1;
}

void play_game(GameState* state)
{
  int turn = 0;
  while (!is_game_over(state))
  {
    //all_turns[num_turns] = *state;
    //turn_hashes[num_turns] = state->hash;
    //global_turns_since_capture = state->turns_since_capture;
    
    int player = ai_current_player();
    printf("\nTURN %d PLAYER: %d (score %+d, %d turns since capture)\n",
      num_turns, player, ai_get_player_score(player), state->turns_since_capture);
    print_board(state);
    if (!play_turn(state))
    {
      printf("\n*** NO MORE MOVES\n");
      return;
    }
    if (++num_turns >= MAX_TURNS)
    {
      printf("\n*** MAX TURNS EXCEEDED\n");
      return;
    }
  }
  
  printf(COLOR_RED "\n***GAME OVER: Player %d won\n" COLOR_NML, is_game_over(state));
  print_board(state);
}

int main(int argc, char** argv)
{
  assert(sizeof(PieceDef)==1); // make sure it's packed properly
  GameState state;
  
  player_strategies[RED] = DEFAULT_STRATEGY;
  player_strategies[BLUE] = DEFAULT_STRATEGY;

  ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 40;
  defaults.max_walk_level = 50;
  ai_init(&defaults);

  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
