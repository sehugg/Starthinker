
#include "ai.h"

#define COLOR_RED "\033[31m"
#define COLOR_BLUE "\033[34m"
#define COLOR_RED_VIS "\033[31;1m"
#define COLOR_BLUE_VIS "\033[34;1m"
#define COLOR_NML "\033[0m"

typedef enum {
  None,
  Pawn,
  Knight,
  Bishop,
  Rook,
  Queen,
  King,
  NUM_PIECE_TYPES
} PieceType;

static int PIECES_PER_TYPE[NUM_PIECE_TYPES] = { 0, 8, 2, 2, 2, 1, 1 };

static char* CHARS_PER_TYPE = ".PNBRQK";

typedef enum {
  WHITE,
  BLACK,
} PlayerType;

#define NPLAYERS 2

typedef struct {
  PieceType type:3;
  PlayerType player:1;
  bool moved:1;
} __attribute__((packed)) PieceDef;

#define BOARDX 8
#define BOARDY 8

typedef uint64_t BoardMask; // 8 x 8 = 64

// bit index of x,y position
#define BI(x,y) ((y)*BOARDX+(x))
// bit mask (1 bit set)
#define BM(x,y) (((x)>=0&&(y)>=0&&(x)<BOARDX&&(y)<BOARDY)?CHOICE(BI(x,y)):0)
// line of 8 squares starting @ x0,y0 and with direction xd,yd
#define LINE8(x0,y0,xd,yd) (BM(x0+xd*0,y0+yd*0) | BM(x0+xd*1,y0+yd*1) | BM(x0+xd*2,y0+yd*2) | BM(x0+xd*3,y0+yd*3) | BM(x0+xd*4,y0+yd*4) | BM(x0+xd*5,y0+yd*5) | BM(x0+xd*6,y0+yd*6) | BM(x0+xd*7,y0+yd*7))

#define I2XY(index,x,y) int x = (index) & 7; int y = (index) >> 3;

typedef struct {
  BoardMask occupied[NPLAYERS];	// 8x8 bitmap for each player
  PieceDef board[BOARDY][BOARDX];
  //uint8_t npieces[NPLAYERS][NUM_PIECE_TYPES];
  uint8_t kingpos[NPLAYERS]; // index of king
  uint8_t enpassant[NPLAYERS]; // if pawn just moved two squares, will be nonzero and index of capture point
  bool incheck[NPLAYERS];
} GameState;

typedef struct {
  int score_incheck;
  int quiescence_level;
  int piece_values[NUM_PIECE_TYPES];
  int piece_squares[BOARDY][BOARDX][NUM_PIECE_TYPES];
} PlayerStrategy;

static PlayerStrategy DEFAULT_STRATEGY = {
  0,
  16,
  { 0, 100, 320, 330, 510, 880, 0 },
};

static int CANONICAL_PIECE_VALUES[NUM_PIECE_TYPES] = { 0, 1, 3, 3, 5, 9, 4 };

PlayerStrategy player_strategies[2];

#define MAX_TURNS 1000

HashCode turn_hashes[MAX_TURNS];
int num_turns;
int num_turns_since_capture;

static int max_rep_inc_depth = 14;

//

static PieceDef MAKEPIECE(PlayerType player, PieceType type)
{
  PieceDef def = { type, player };
  return def;
}

int count_bits(BoardMask v)
{
  int c;
  for (c = 0; v; c++)
    v &= v - 1; // clear the least significant bit set
  return c;
}

//

void print_board(const GameState* state);

bool is_valid_space(int x, int y)
{
  return !(x >= BOARDX || x < 0 || y >= BOARDY || y < 0);
}

int get_piece_score(const PieceDef def, int xx, int yy)
{
  assert(is_valid_space(xx,yy));
  if (def.type == 0)
    return 0;
    
  const PlayerStrategy* strategy = &player_strategies[def.player];
  int n = strategy->piece_values[def.type];
  n += strategy->piece_squares[yy][xx][def.type];
  return n;
}

void set_piece(const GameState* state, int x, int y, const PieceDef def)
{
  assert(is_valid_space(x,y));
  const PieceDef old = state->board[y][x];
  if (old.type)
  {
    // can't replace king
    if (old.type == King && def.type && def.type != King)
    {
      printf("Cannot replace King @ %d,%d with %c!\n", x, y, CHARS_PER_TYPE[def.type]);
      print_board(state);
      assert(0);
    }
    DEBUG("Removing piece @ %d,%d (type %c)\n", x, y, CHARS_PER_TYPE[old.type]);
    //DEC(state->npieces[old.player][old.type]);
    ai_add_player_score(old.player, -get_piece_score(old,x,y)); //TODO: wasteful
    SET(state->board[y][x], MAKEPIECE(0, None));
    // TODO: could set a single byte instead
    SET(state->occupied[old.player], state->occupied[old.player] & ~BM(x,y));
  }
  if (def.type)
  {
    SET(state->board[y][x], def);
    // TODO: could set a single byte instead
    SET(state->occupied[def.player], state->occupied[def.player] | BM(x,y));
    DEBUG("Adding piece @ %d,%d (type %c)\n", x, y, CHARS_PER_TYPE[state->board[y][x].type]);
    //INC(state->npieces[def.player][def.type]);
    ai_add_player_score(def.player, get_piece_score(def,x,y));
    // if moving king, record position
    if (def.type == King)
    {
      SET(state->kingpos[def.player], BI(x,y));
    }
  }
}

void init_game(GameState* state)
{
  int player,type,i;
  num_turns = 0;
  num_turns_since_capture = 0;
  memset(state, 0, sizeof(GameState));
  for (player=WHITE; player<=BLACK; player++)
  {
    int y = player ? BOARDY-1 : 0;
    set_piece(state, 0, y, MAKEPIECE(player, Rook));
    set_piece(state, 1, y, MAKEPIECE(player, Knight));
    set_piece(state, 2, y, MAKEPIECE(player, Bishop));
    set_piece(state, 3, y, MAKEPIECE(player, Queen));
    set_piece(state, 4, y, MAKEPIECE(player, King));
    set_piece(state, 5, y, MAKEPIECE(player, Bishop));
    set_piece(state, 6, y, MAKEPIECE(player, Knight));
    set_piece(state, 7, y, MAKEPIECE(player, Rook));
    y = player ? BOARDY-2 : 1;
    for (int x=0; x<BOARDX; x++)
      set_piece(state, x, y, MAKEPIECE(player, Pawn));
  }
}

void print_board(const GameState* state)
{
  printf("\n");
  int x,y;
  for (y=BOARDY-1; y>=0; y--)
  {
    printf("  %d  ", y+1); // TODO: use proper coords everywhere
    for (x=0; x<BOARDX; x++)
    {
      char ch;
      const PieceDef* def = &state->board[y][x];
      ch = is_valid_space(x,y) ? '.' : '#';
      if (def->type)
      {
        ch = CHARS_PER_TYPE[def->type];
        if (def->type == King && state->incheck[def->player])
          printf(def->player==WHITE ? COLOR_RED_VIS : COLOR_BLUE_VIS);
        else
          printf(def->player==WHITE ? COLOR_RED : COLOR_BLUE);
      }
      for (int p=0; p<2; p++)
        if (state->enpassant[p] && state->enpassant[p] == BI(x,y))
          ch += p+2;
      printf("%c", ch);
      printf(COLOR_NML);
    }
    printf("\n");
  }
  printf("\n     abcdefgh\n     01234567\n\n");
  fflush(stdout);
}

void reward(const GameState* state, PieceDef def, int x, int y, int value)
{
  player_strategies[def.player].piece_squares[y][x][def.type] += value;
}

void reward_all(const GameState* state, int player, int value)
{
  if (value == 0)
    return;
  for (int i=0; i<BOARDX*BOARDY; i++)
  {
    PieceDef def = state->board[0][i];
    if (def.type)
    {
      player_strategies[def.player].piece_squares[0][i][def.type] += (def.player == player) ? value : -value;
    }
  }
}

void load_piece_square_values()
{
  FILE* f = fopen("chess.psv","rb");
  if (!f || !fread(player_strategies, sizeof(player_strategies), 1, f))
    fprintf(stderr, "Could not read chess.psv, using defaults\n");
  printf("Read chess.psv\n");
  fclose(f);
  /*
  // normalize
  for (int p=0; p<2; p++)
  {
    for (int i=0; i<BOARDX*BOARDY*NUM_PIECE_TYPES; i++)
    {
      int* pv = &player_strategies[p].piece_squares[0][0][i];
      if (*pv > 50) *pv = 50;
      else if (*pv < -50) *pv = -50;
    }
  }
  */
}

void save_piece_square_values()
{
  FILE* f = fopen("chess.psv","wb");
  if (!f || !fwrite(player_strategies, sizeof(player_strategies), 1, f)) abort();
  printf("Wrote chess.psv\n");
  fclose(f);
}

void print_piece_square_values()
{
  for (PieceType type=0; type<NUM_PIECE_TYPES; type++)
  {
    for (int player=0; player<2; player++)
    {
      printf("Player %d, Piece Type %c:\n", player, CHARS_PER_TYPE[type]);
      for (int y=0; y<BOARDY; y++)
      {
        for (int x=0; x<BOARDX; x++)
        {
          printf("%8d ", player_strategies[player].piece_squares[y][x][type]);
        }
        printf("\n");
      }
      printf("\n");
    }
  }
}

int move_src  = 0;
int move_dest = 0;

int play_turn(const GameState* state);

int is_threatened(const GameState* state, int x0, int y0, int player);

bool compute_incheck(const GameState* state)
{
  // compute king check status
  int player = ai_current_player();
  for (int p=0; p<2; p++)
  {
    I2XY(state->kingpos[p], kingx, kingy);
    bool check = is_threatened(state, kingx, kingy, p);
    if (check != state->incheck[p])
    {
      SET(state->incheck[p], check);
      // temporarily penalize (the other) checked player
      if (p != player)
        ai_add_player_score(p, player_strategies[player].score_incheck * (check ? -1 : 1));
    }
  }
  return state->incheck[player];
}

int promote_piece(const void* pstate, ChoiceIndex promote_to_type)
{
  const GameState* state = pstate;

  I2XY(move_dest, x, y);
  DEBUG("Promoting piece @ %d,%d to %c\n", x, y, CHARS_PER_TYPE[promote_to_type]);
  PieceDef piece = state->board[y][x];
  piece.type = promote_to_type;
  set_piece(state, x, y, piece);

  // check king check status here too  
  if (compute_incheck(state))
    return 0;

  // next player
  if (ai_next_player())
  {
    play_turn(state);
  }
  return 1;
}

int make_move(const void* pstate, ChoiceIndex dest)
{
  const GameState* state = pstate;

  I2XY(move_src, x1, y1);
  I2XY(dest, x2, y2);
  if (!ai_is_searching()) SETGLOBAL(move_dest, dest); // HACK: needed to record move later
  PieceDef piece = state->board[y1][x1];
  PieceDef piece2 = state->board[y2][x2];
  int player = ai_current_player();
  DEBUG("P%d moving piece from %d,%d to %d,%d\n", player, x1, y1, x2, y2);
  assert(piece.player == player);
  assert(piece.type);
  
  // is this an en passant capture?
  if (dest != 0 && piece.type == Pawn && dest == state->enpassant[player^1])
  {
    int pawny = y2 + (player?1:-1); // look one space in front of capture pos
    if (state->board[y2][x2].type != 0)
    {
      printf("***En passant move failed: [%d/%d] P%d %d %c %d,%d -> %d,%d/%d\n", search_level, ai_get_mode(), player, dest, CHARS_PER_TYPE[piece.type], x1, y1, x2, y2, pawny);
      print_board(state);
      return 0; // TODO: fix it
    }
    assert(state->board[y2][x2].type == 0); // there should be nothing on our destination square
    assert(state->board[pawny][x2].type == Pawn); // there should be a pawn there
    set_piece(state, x2, pawny, MAKEPIECE(0,0)); // get rid of that pawn
  }
  
  // move player from src to dest
  set_piece(state, x1, y1, MAKEPIECE(0,0));
  piece.moved = true;
  set_piece(state, x2, y2, piece);
  
  // castling? move rook also
  if (piece.type == King && (x2-x1 == 2 || x2-x1 == -2))
  {
    int xr1 = x2 > x1 ? 7 : 0;
    int xr2 = x2 > x1 ? x2-1 : x2+1;
    assert(y1 == y2);
    assert(state->board[y1][xr1].type == Rook);
    set_piece(state, xr1, y1, MAKEPIECE(0,0));
    PieceDef rook = { Rook, player, true };
    set_piece(state, xr2, y2, rook);
    DEBUG("Player %d castled to %d,%d; rook moved to %d,%d\n", player, x2, y2, xr2, y2);
  }
  // promote this pawn?
  else if (piece.type == Pawn && y2 == (player?0:BOARDY-1))
  {
    // have to do this for backtracking
    SETGLOBAL(move_dest, dest);
    // player chooses which piece to promote to
    return ai_choice(state, 0, promote_piece, 0, (1<<Queen)|(1<<Rook)|(1<<Bishop)|(1<<Knight));
  }
  // en passant opportunity?
  else if (piece.type == Pawn && (y2-y1 == 2 || y2-y1 == -2))
  {
    assert(player ^ (y2>y1));
    SET(state->enpassant[player], player ? dest+BOARDX : dest-BOARDX); // set to capture point (one behind pawn) 
  }
  
  // we cannot end our turn with our king in check
  if (compute_incheck(state))
    return 0;

  // next player
  if (ai_next_player())
  {
    play_turn(state);
  } else {
    // we are in play mode
    // reward captures by updating piece-square table
    if (piece2.type)
    {
      //int value = CANONICAL_PIECE_VALUES[piece2.type]; // value of captured piece
      //reward_all(state, piece.player, 1);
      reward(state, piece, x1, y1, 1);
      reward(state, piece2, x2, y2, -1);
    }
    // reset 50-move counter whenever we move a pawn, or when we capture
    if (piece2.type || piece.type == Pawn)
      num_turns_since_capture = 0;
  }
  return 1;
}

static BoardMask ELEFT[BOARDX];
static BoardMask ERIGHT[BOARDX];
static BoardMask ETOP[BOARDY];
static BoardMask EBOTTOM[BOARDY];
static BoardMask KNIGHTMOVES;
static BoardMask ROYALMOVES;

void init_masks()
{
  BoardMask left = LINE8(0,0,0,1);
  BoardMask right = LINE8(7,0,0,1);
  BoardMask top = LINE8(0,0,1,0);
  BoardMask bottom = LINE8(0,7,1,0);
  for (int i=0; i<8; i++)
  {
    ELEFT[i] = left;
    ERIGHT[i] = right;
    ETOP[i] = top;
    EBOTTOM[i] = bottom;
    left |= (left << 1);
    right |= (right >> 1);
    top |= (top << 8);
    bottom |= (bottom >> 8);
  }
  KNIGHTMOVES = BM(1,0)|BM(0,1)|BM(0,3)|BM(1,4)|BM(3,4)|BM(4,3)|BM(4,1)|BM(3,0);
  ROYALMOVES = BM(0,0)|BM(1,0)|BM(2,0)|BM(0,1)|BM(2,1)|BM(0,2)|BM(1,2)|BM(2,2);
}

static BoardMask offset(BoardMask mask, int dx, int dy)
{
  if (dx < 0)
    mask = (mask & ~ELEFT[-dx-1]) >> -dx;
  else if (dx > 0)
    mask = (mask & ~ERIGHT[dx-1]) << dx;
  if (dy < 0)
    mask >>= -dy*BOARDX;
  else if (dy > 0)
    mask <<= dy*BOARDX;
  return mask;
}

static BoardMask project(int x, int y, int dx, int dy, BoardMask us, BoardMask them)
{
  BoardMask m=0;
  BoardMask p=BM(x+dx,y+dy);
  while (p) {
    if (p&us) break; // one of our pieces
    m |= p;
    if (p&them) break; // capture piece
    p = offset(p,dx,dy);
  }
  return m;
}

bool can_castle(const GameState* state, int player, int y, int xr, BoardMask both)
{
  PieceDef rook = state->board[y][xr];
  // rook must not have moved
  if (rook.type == Rook && !rook.moved)
  {
    // no pieces between king and rook
    BoardMask castlepath = (xr==0 ? (BM(1,0)|BM(2,0)|BM(3,0)) : (BM(5,0)|BM(6,0))) << (y*BOARDX);
    if (castlepath & both)
      return false;
    // square next to king on castling side cannot be under attack
    int x1 = xr==0 ? 3 : 5;
    if (is_threatened(state, x1, y, player))
    {
      DEBUG("Player %d cannot castle; path threatened @ %d,%d\n", player, x1, y);
      return false;
    }
    return true;
  } else
    return false;
}

BoardMask get_valid_moves(const GameState* state, int x, int y, PieceDef def)
{
#define PROJECT(dx,dy) project(x,y,dx,dy,us,them)

  BoardMask us   = state->occupied[def.player];
  BoardMask them = state->occupied[def.player^1];
  BoardMask both = us | them;
  switch (def.type)
  {
    case Pawn:
    {
      int dir = def.player ? -1 : 1;
      // move one space forward if unoccupied
      BoardMask mask = BM(x,y+dir) & ~both;
      // move two spaces forward if first move and unoccupied
      if (y==(def.player?BOARDY-2:1) && mask)
      {
        mask |= BM(x,y+dir*2) & ~both;
      }
      // capture piece diagonally
      mask |= (BM(x-1,y+dir) | BM(x+1,y+dir)) & them;
      // en passant opportunity?
      if (state->enpassant[def.player^1])
      {
        // capture en passant piece diagonally
        mask |= (BM(x-1,y+dir) | BM(x+1,y+dir)) & CHOICE(state->enpassant[def.player^1]);
      }
      return mask;
    }
    case Knight:
    {
      // KNIGHTMOVES mask is offset by 2,2
      return offset(KNIGHTMOVES, x-2, y-2) & ~us;
    }
    case Bishop:
    case Rook:
    case Queen:
    {
      BoardMask m = 0;
      // horizontal
      if (def.type == Rook || def.type == Queen)
        m |= PROJECT(-1,0) | PROJECT(1,0) | PROJECT(0,1) | PROJECT(0,-1);
      // diagonal
      if (def.type == Bishop || def.type == Queen)
        m |= PROJECT(-1,-1) | PROJECT(1,1) | PROJECT(-1,1) | PROJECT(1,-1);
      return m;
    }
    case King:
    {
      // ROYALMOVES mask is offset by 1,1
      BoardMask m = offset(ROYALMOVES, x-1, y-1) & ~us;
      // can castle? king must not have moved, and must not be in check
      if (!def.moved && !state->incheck[def.player])
      {
        assert(x == 4);
        assert(y == def.player*7);
        // check to see if Rooks moved
        if (can_castle(state, def.player, y, 0, both))
          m |= BM(x-2,y);
        if (can_castle(state, def.player, y, BOARDX-1, both))
          m |= BM(x+2,y);
      }
      return m;
    }
    default:
      assert(0);
  }
}

#define FOR_EACH_BIT(mask,index) for (int index=0; mask; index++, mask >>= 1) if (mask&1)

int can_capture(const GameState* state, int attacking_player, BoardMask defending, BoardMask attacking, int allowed_pieces)
{
  // only look at attacking player's pieces
  attacking &= state->occupied[attacking_player];
  //DEBUG("What if player %d attacked %llx with %llx (allowed types 0x%x)\n", attacking_player, defending, attacking, allowed_pieces);
  // iterate thru each attacking piece
  FOR_EACH_BIT(attacking, index)
  {
    I2XY(index, x, y);
    PieceDef def = state->board[y][x];
    // skip it if it's not an allowed piece
    if (allowed_pieces & (1<<def.type))
    {
      DEBUG("Check from %d,%d (%c)\n", x, y, CHARS_PER_TYPE[def.type]);
      // do the valid moves of this piece intersect our defending pieces?
      if (defending & get_valid_moves(state, x, y, state->board[y][x]))
      {
        DEBUG("Player %d can threaten piece @ 0x%llx with piece @ %d,%d\n", attacking_player, defending, x, y);
        //print_board(state);
        return index+1;
      }
    }
  }
  return 0;
}

int is_threatened(const GameState* state, int x0, int y0, int player)
{
  // compute masks for various attack vectors
  // now check those bitmasks to see if any of those pieces have a feasible attack
  BoardMask pos = BM(x0,y0);
  return
    can_capture(state, player^1, pos, get_valid_moves(state, x0, y0, MAKEPIECE(player, Knight)), (1<<Knight)) ||
    can_capture(state, player^1, pos, get_valid_moves(state, x0, y0, MAKEPIECE(player, Rook)), (1<<Rook)|(1<<Queen)|(1<<King)) ||
    can_capture(state, player^1, pos, get_valid_moves(state, x0, y0, MAKEPIECE(player, Bishop)), (1<<Bishop)|(1<<Pawn)|(1<<Queen)|(1<<King));
}

int choose_destination(const void* pstate, ChoiceIndex pos)
{
  const GameState* state = pstate;
  SETGLOBAL(move_src, pos);
  I2XY(pos, x, y);
  PieceDef def = state->board[y][x];
  BoardMask movemask = get_valid_moves(state, x, y, def);
  // if in Random mode, or high depth level, try capture moves first
  if (ai_get_mode() == AI_RANDOM || search_level >= player_strategies[def.player].quiescence_level)
  {
    BoardMask capturemask = movemask & state->occupied[def.player^1];
    if (capturemask)
    {
      DEBUG("Capture moves = %llx\n", capturemask);
      if (ai_choice(pstate, 0, make_move, 0, capturemask))
        return 1;
      movemask &= ~capturemask;
    }
    // past the horizon, if there are no captures in search mode and not in check, exit
    if (ai_get_mode() == AI_SEARCH && !state->incheck[0] && !state->incheck[1])
      return 1;
  }
  DEBUG("@ %d,%d valid moves = %llx\n", x, y, movemask);
  return movemask ? ai_choice(pstate, 0, make_move, 0, movemask) : 0;
}

int play_turn(const GameState* state)
{
  int player = ai_current_player();
  // reset en passant flag
  if (state->enpassant[player]) { SET(state->enpassant[player],0); }
  // look at all moves for all pieces
  DEBUG("P%d occupied = %llx\n", player, state->occupied[player]);
  if (!ai_choice(state, 0, choose_destination, 0, state->occupied[player]))
  {
    // player cannot move, checkmate
    DEBUG("Player %d: no moves\n", player);
    ai_set_player_score(player^1, MAX_SCORE);
    ai_game_over();
    // penalty for checkmated king, reward for non-checkmated king
    if (!ai_is_searching())
    {
      /*
      for (int p=0; p<num_players; p++)
      {
        I2XY(state->kingpos[p], kx, ky);
        reward(state, state->board[ky][kx], kx, ky, p==player ? -10 : 10);
      }
      */
      reward_all(state, player, -1);
      reward_all(state, player^1, 1);
    }
    return 0;
  } else
    return 1;
}

int is_repeated_state(const GameState* state)
{
  HashCode hash = ai_current_hash();
  turn_hashes[num_turns] = hash;
  int count = 0;
  for (int i=0; i<=num_turns; i++)
  {
    if (turn_hashes[i] == hash)
      count++;
  }
  return count;
}

void play_game(GameState* state)
{
  do
  {
    int player = ai_current_player();
    printf("\nTURN %d PLAYER: %d (score %+d, %d since capture)\n",
      num_turns, player, ai_get_player_score(player), num_turns_since_capture);
    print_board(state);
    // threefold-repetition rule
    if (is_repeated_state(state) >= 3)
    {
      printf("\n***GAME OVER: Player %d threefold repetition on turn %d\n", player, num_turns);
      return;
    }
    // 50-move rule
    if (++num_turns_since_capture >= 50)
    {
      printf("\n***GAME OVER: %d turns since capture on turn %d\n", num_turns_since_capture, num_turns);
      return;
    }
    // if two-fold repetition, increase depth level
    if (player == 0 && is_repeated_state(state) >= 2 && ai_player_settings(0)->max_search_depth < max_rep_inc_depth)
    {
      printf("\nTwofold repetition: increasing depth to %d\n", ai_player_settings(0)->max_search_depth);
      ai_player_settings(0)->max_search_depth += 2;
      ai_player_settings(1)->max_search_depth += 2;
    }
    // if player cannot play turn, he loses (checkmate)
    if (!play_turn(state))
    {
      printf("\n***GAME OVER: Player %d was checkmated on turn %d\n", player, num_turns);
      return;
    }
  } while (num_turns++ < MAX_TURNS);
  printf("\n*** MAX TURNS EXCEEDED\n");
}

#include "epd.c"

int main(int argc, char** argv)
{
  assert(sizeof(PieceDef)==1); // make sure it's packed properly
  
  player_strategies[WHITE] = DEFAULT_STRATEGY;
  player_strategies[BLACK] = DEFAULT_STRATEGY;
  load_piece_square_values();

  int argi = ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 2;
  defaults.max_search_level = 20;
  defaults.max_walk_level = -1; // TODO: why do we get en passant errors when this is positive?
  ai_init(&defaults);
  init_masks();

  // extra arguments? if so, parse EPD file for each
  if (argi < argc)
  {
    while (argi < argc)
      if (!parse_epd_file(argv[argi++]))
        return 1;
    return 0;
  }

  GameState state;
  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  print_piece_square_values();
  save_piece_square_values();
  
  return 0;
}
