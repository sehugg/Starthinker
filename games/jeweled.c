
#include "ai.h"
#include <ncurses.h>

typedef enum {
  BLANK,
  OUTOFBOUNDS,
  STONE,
  BOMB,
  PLAIN,
  VSTRIPED,
  HSTRIPED,
  WRAPPED,
} GemType;

#define IS_GEM(t) ((t) >= BOMB)
#define HAS_COLOR(t) ((t) >= PLAIN)
#define IS_SPECIAL(t) (IS_GEM(t) && (t) != PLAIN)

typedef enum {
  GCRed,
  GCOrange,
  GCYellow,
  GCGreen,
  GCBlue,
  GCPurple,
  GCWildcard,
  GCNone,
} GemColor;

#define NCOLORS 8
#define NREALCOLORS 6
#define NDIRS 4

static char* GEM_TYPES = ".XO@*|=%";
static int GEM_COLORS[8] = { COLOR_RED, 7, COLOR_YELLOW, COLOR_GREEN, COLOR_BLUE, COLOR_MAGENTA, COLOR_BLACK, COLOR_BLACK };

#define BOARDX 9
#define BOARDY 9
#define PADDING 2

typedef struct
{
  GemType type:3;
  GemColor color:3;
} __attribute__((packed)) Cell;

typedef struct
{
  uint64_t l,h;
} BoardMask;

typedef struct
{
  Cell board[BOARDY+PADDING*2][16]; // padding on all sides, so coords are all 2-indexed
  uint8_t jelly[BOARDY][16];
  BoardMask colormask[NCOLORS];
  int goaljelly;
} GameState;

typedef struct
{
  BoardMask mask;
  GemType type;
} MatchTemplate;

//

bool deterministic = false;

#define NHORIZMASKS3 ((BOARDX-2)*BOARDY)
#define NVERTMASKS3 ((BOARDY-2)*BOARDX)
#define NMASKS3 (NHORIZMASKS3+NVERTMASKS3)

static BoardMask MASKS3[NMASKS3];
static BoardMask ADJACENT11;

#define rnd(n) (((unsigned int)random()) % n)

#define GEM_SCORE 1
#define JELLY_SCORE 10
#define STONE_SCORE 31

static BoardMask bm_point(int x, int y)
{
  int n = x + y*BOARDX;
  BoardMask bm;
  if (n < 64)
  {
    bm.l = CHOICE(n);
    bm.h = 0;
  } else {
    bm.l = 0;
    bm.h = CHOICE((n-64));
  }
  return bm;
}

static BoardMask bm_or(const BoardMask a, const BoardMask b)
{
  BoardMask bm = { a.l|b.l, a.h|b.h };
  return bm;
}

static BoardMask bm_and(const BoardMask a, const BoardMask b)
{
  BoardMask bm = { a.l&b.l, a.h&b.h };
  return bm;
}

static BoardMask bm_not(const BoardMask a)
{
  BoardMask bm = { ~a.l, ~a.h };
  return bm;
}

static bool bm_empty(const BoardMask a)
{
  return !a.l && !a.h;
}

static BoardMask bm_shl(const BoardMask a, int bits)
{
  BoardMask bm = { a.l<<bits, (a.h<<bits)|(a.l>>(64-bits)) };
  return bm;
}

static BoardMask bm_shr(const BoardMask a, int bits)
{
  BoardMask bm = { a.l>>bits, (a.h>>bits)|(a.l<<(64-bits)) };
  return bm;
}

static bool bm_equals(const BoardMask a, const BoardMask b)
{
  return a.l==b.l && a.h==b.h;
}

static int bitcount(uint64_t x)
{
  int n = 0;
  while (x)
  {
    n += x&1;
    x >>= 1;
  }
  return n;
}

static int bm_count(const BoardMask a)
{
  return bitcount(a.h) + bitcount(a.l);
}

#define COORDFMT "%c%d"
static char* COL_CHARS = " 123456789";
#define COORDS(x,y) COL_CHARS[x], y

static Cell MAKECELL(GemType type, GemColor color)
{
  Cell c = { type, color };
  return c;
}

static const Cell* get_cell(const GameState* state, int x, int y)
{
  return &state->board[y+PADDING][x+PADDING];
}

const void set_cell(const GameState* state, int x, int y, Cell c)
{
  const Cell* dst = get_cell(state, x, y);
  if (dst->color != GCNone)
  {
    SET(state->colormask[dst->color], bm_and(state->colormask[dst->color], bm_not(bm_point(x,y))));
  }
  
  SET(*dst, c);
  
  if (c.color != GCNone)
  {
    SET(state->colormask[c.color], bm_or(state->colormask[c.color], bm_point(x,y)));
  }
  if (c.type == STONE)
  {
    SET(state->goaljelly, state->goaljelly+2);
  }
}

void print_board(const GameState* state)
{
  printf("\nBOARD (score %d, jelly %d):\n\n", ai_get_player_score(ai_current_player()), state->goaljelly);
  printf("    A  B  C  D  E  F  G  H  J\n\n");
  int x,y;
  int tj=0;
  for (y=0; y<=BOARDY-1; y++)
  {
    printf(" %2d ", y+1);
    for (x=0; x<=BOARDX-1; x++)
    {
      char ch;
      Cell c = *get_cell(state, x, y);
      ch = GEM_TYPES[c.type];
      int col = GEM_COLORS[c.color];
      if (HAS_COLOR(c.type))
        printf("\033[3%dm%c\033[0m", col, ch);
      else
        printf("%c", ch);
      if (state->jelly[y][x])
        printf("%d ", state->jelly[y][x]);
      else
        printf("  ");
      tj += state->jelly[y][x];
      if (c.type == STONE) tj += 2;
      if (c.color != GCNone)
      {
        assert(!bm_empty(bm_and(state->colormask[c.color], bm_point(x,y))));
      }
    }
    printf("\n");
  }
  printf("    A  B  C  D  E  F  G  H  J\n\n");
  assert(tj == state->goaljelly);
}

static int move_row = -1;
static int move_col = -1;

int play_turn(const GameState* state);

int remove_stone(const GameState* state, int x, int y)
{
  int score = 0;
  Cell c = *get_cell(state, x, y);
  if (c.type == STONE)
  {
    c.type = BLANK;
    SET(state->jelly[y][x], 2);
    set_cell(state, x, y, c);
    score += STONE_SCORE;
    DEBUG("remove_stone(%d,%d): %d\n", x, y, score);
  }
  return score;
}

// requires score:int by in scope
#define CLEAR_CELLS_WITH(fn) do { \
  for (int cury=0; cury<BOARDY; cury++) { \
    for (int curx=0; curx<BOARDX; curx++) { \
      const Cell curcell = *get_cell(state, curx, cury); \
      if (IS_GEM(curcell.type) && (fn)) { score += remove_gem(state, curx, cury); } \
    } \
  } \
} while (0)

int remove_gem(const GameState* state, int x, int y)
{
  int score = GEM_SCORE;
  // decrease jelly, if present
  int j = state->jelly[y][x];
  if (j)
  {
    DEC(state->jelly[y][x]);
    DEC(state->goaljelly);
    score += JELLY_SCORE;
  }
  const Cell blankcell = { };
  // blank out cell
  Cell oldc = *get_cell(state, x, y);
  set_cell(state, x, y, blankcell);
  // remove adjacent stones
  score += remove_stone(state, x-1, y);
  score += remove_stone(state, x+1, y);
  score += remove_stone(state, x, y-1);
  score += remove_stone(state, x, y+1);
  // special gem type? (may recurse)
  if (oldc.type != PLAIN) DEBUG("removing special type %c @ %d,%d\n", GEM_TYPES[oldc.type], x, y);
  switch (oldc.type)
  {
    case BOMB:
      CLEAR_CELLS_WITH(curcell.color == oldc.color);
      break;
    case HSTRIPED:
      CLEAR_CELLS_WITH(cury == y);
      break;
    case VSTRIPED:
      CLEAR_CELLS_WITH(curx == x);
      break;
    case WRAPPED:
      CLEAR_CELLS_WITH(abs(curx-x)<=1 && abs(cury-y)<=1);
      break;
    default:
      break;
  }
  return score;
}

int remove_gems(const GameState* state, BoardMask bm, GemColor color)
{
  int ngems = bm_count(bm);
  GemType spectype = BLANK;
  // 5 across?
  if (ngems > 3)
  {
    bool hasvert = !bm_empty(bm_and(bm, bm_shr(bm, BOARDX)));
    if (ngems >= 5 && hasvert)
      spectype = WRAPPED;
    else if (ngems >= 5)
      spectype = BOMB;
    else
      spectype = hasvert ? VSTRIPED : HSTRIPED;
  }
  DEBUG("remove_gems: %d gems, adding %c (%llx %llx)\n", ngems, GEM_TYPES[spectype], bm.h, bm.l);
  int score = 0;
  int px = -1;
  int py = -1;
  int fx = -1;
  int fy = -1;
  // TODO: faster
  for (int y=0; y<BOARDY; y++)
  {
    for (int x=0; x<BOARDX; x++)
    {
      if (!bm_empty(bm_and(bm, bm_point(x,y))))
      {
        score += remove_gem(state,x,y);
        if (x == move_col && y == move_row)
        {
          px = x;
          py = y;
        }
        if (fx < 0)
        {
          fx = x;
          fy = y;
        }
      }
    }
  }
  // add special gem
  if (spectype != BLANK)
  {
    if (px < 0 && py < 0)
    {
      px = fx;
      py = fy;
    }
    set_cell(state, px, py, MAKECELL(spectype, color));
    DEBUG("added %c @ %d,%d\n", GEM_TYPES[spectype], px, py);
  }
  DEBUG("remove_gems: color %d, mask %llx %llx, score = %d\n", color, bm.h, bm.l, score);
  return score;
}

// TODO: ai_chance
Cell random_gem()
{
  static GemColor nextcolor = 0;
  
  if (deterministic)
  {
    SETGLOBAL(nextcolor, (nextcolor+1) % NREALCOLORS);
  } else {
    nextcolor = rnd(NREALCOLORS);
  }
  
  return MAKECELL(PLAIN, nextcolor);
}

int move_gems_down(const GameState* state)
{
  int total = 0;
  for (int x=0; x<BOARDX; x++)
  {
    int srcy = BOARDY-1;
    int desty = BOARDY-1;
    while (desty >= 0)
    {
      // skip blank cells
      if (get_cell(state, x, desty)->type == STONE)
      {
        srcy--;
        desty--;
        continue;
      }
      if (srcy >= 0)
      {
        const Cell* src = get_cell(state, x, srcy);
        if (src->type == BLANK)
        {
          srcy--;
          continue;
        }
      }
      if (srcy < desty)
      {
        Cell src = (srcy >= 0) ? *get_cell(state, x, srcy) : random_gem();
        //DEBUG("move %d->%d %d %d\n", srcy, desty, src.type, src.color);
        set_cell(state, x, desty, src);
        total++;
      }
      desty--;
      srcy--;
    }
  }
  DEBUG("move_gems_down: total %d\n", total);
  return total;
}

int find_all_matches(const GameState* state);

BoardMask find_more_matches(BoardMask cbm, BoardMask curmatch, int starti)
{
  assert(!bm_empty(curmatch));
  DEBUG("find_more_matches: mask %llx %llx curmatch %llx %llx start %d\n", cbm.h, cbm.l, curmatch.h, curmatch.l, starti);
  for (int i=starti; i<NMASKS3; i++)
  {
    BoardMask bm = bm_and(cbm, MASKS3[i]);
    if (bm_equals(bm, MASKS3[i]) && !bm_empty(bm_and(curmatch, bm)))
    {
      curmatch = bm_or(curmatch, bm);
      DEBUG("  found mask index %d - bm %llx %llx\n", i, curmatch.h, curmatch.l);
    }
  }
  return curmatch;
}

int find_color_matches(const GameState* state, BoardMask cbm, GemColor color)
{
  int score = 0;
  DEBUG("find_color_matches: color %d mask %llx %llx\n", color, cbm.h, cbm.l);
  for (int i=0; i<NMASKS3; i++)
  {
    BoardMask bm = bm_and(cbm, MASKS3[i]);
    if (bm_equals(bm, MASKS3[i]))
    {
      BoardMask bm2 = find_more_matches(cbm, bm, i+1);
      int n = bm_count(bm2);
      score += remove_gems(state, bm2, color);
      cbm = bm_and(cbm, bm_not(bm2));
    }
  }
  DEBUG("find_color_matches: score %d\n", score);
  return score;
}

int find_matches(const GameState* state, int colorflags, int colflags, int rowflags)
{
  assert(BOARDX==BOARDY);
  int total = 0;
  DEBUG("find_matches(colors %x, cols %x, rows %x)\n", colorflags, colflags, rowflags);
  for (int color=0; color<NREALCOLORS; color++)
  {
    if (CHOICE(color) & colorflags)
    {
      // do we have a chance at 3 across?
      BoardMask cbm = state->colormask[color];
      bool found3 = false;
      {
        BoardMask cbm1 = bm_shl(cbm, 1);
        BoardMask cbm2 = bm_shl(cbm1, 1);
        BoardMask combined = bm_and(cbm, bm_and(cbm1, cbm2));
        DEBUG("color %d horiz: %llx %llx\n", color, combined.h, combined.l);
        found3 |= !bm_empty(combined);
      }
      // what about 3 down?
      if (!found3)
      {
        BoardMask cbm1 = bm_shl(cbm, BOARDX);
        BoardMask cbm2 = bm_shl(cbm, BOARDX);
        BoardMask combined = bm_and(cbm, bm_and(cbm1, cbm2));
        DEBUG("color %d vert : %llx %llx\n", color, combined.h, combined.l);
        found3 |= !bm_empty(combined);
      }
      if (found3)
      {
        total += find_color_matches(state, cbm, color);
      }
    }
  }
  DEBUG("find matches, total = %d\n", total);
  if (!total)
    return 0;
    
  SETGLOBAL(move_row,-1);
  SETGLOBAL(move_col,-1);
    
  while (1)
  {
    if (!ai_is_searching() && CANDEBUG) print_board(state);
    move_gems_down(state);
    if (!ai_is_searching() && CANDEBUG) print_board(state);
    int score = find_all_matches(state);
    if (!score)
      break;
    DEBUG("  score = %d\n", score);
    total += score;
  }
  
  return total;
}

int find_all_matches(const GameState* state)
{
  return find_matches(state, MASK(NREALCOLORS), MASK(BOARDX), MASK(BOARDY));
}

bool has_adjacent_color(const GameState* state, int x, int y, GemColor color)
{
  BoardMask adjmask;
  int i = x + y*BOARDX;
  if (i < BOARDX+1)
    adjmask = bm_shr(ADJACENT11, (BOARDX+1)-i);
  else
    adjmask = bm_shl(ADJACENT11, i-(BOARDX+1));
  return !bm_empty(bm_and(state->colormask[color], adjmask));
}

static int DIR_X[4] = { 0, -1, 0, 1 };
static int DIR_Y[4] = { -1, 0, 1, 0 };

int make_move(const void* pstate, ChoiceIndex dir)
{
  const GameState* state = pstate;
  int dest_row = move_row + DIR_Y[dir];
  int dest_col = move_col + DIR_X[dir];
  const Cell* src = get_cell(state, move_col, move_row);
  const Cell* dest = get_cell(state, dest_col, dest_row);
  // only swap two candies
  // TODO: special candies combinations
  assert(IS_GEM(src->type));
  if (IS_GEM(dest->type) && src->color != dest->color &&
    (has_adjacent_color(state, dest_col, dest_row, src->color) || has_adjacent_color(state, move_col, move_row, dest->color))
    )
  {
    DEBUG("swap %d,%d with %d,%d\n", move_col, move_row, dest_col, dest_row);
    Cell sc = *src;
    Cell dc = *dest;
    set_cell(state, dest_col, dest_row, sc);
    set_cell(state, move_col, move_row, dc);
    // at least one match?
    int score = find_matches(state, CHOICE(sc.color)|CHOICE(dc.color), CHOICE(move_col)|CHOICE(dest_col), CHOICE(move_row)|CHOICE(dest_row));
    if (score > 0)
    {
      ai_add_player_score(ai_current_player(), score);
      if (ai_next_player())
        return play_turn(state); // if in search mode
      else
        return 1;
    }
  }
  return 0;
}

int choose_col(const void* pstate, ChoiceIndex col)
{
  const GameState* state = pstate;
  // only choose candies
  if (IS_GEM(get_cell(state, col, move_row)->type))
  {
    SETGLOBAL(move_col, col);
    return ai_choice(state, 0, make_move, 0, RANGE(0,NDIRS));
  } else
    return 0;
}

int choose_row(const void* pstate, ChoiceIndex row)
{
  const GameState* state = pstate;
  if (true)
  {
    SETGLOBAL(move_row, row);
    return ai_choice(state, 0, choose_col, 0, RANGE(0,BOARDX));
  } else
    return 0;
}

void shuffle_board(const GameState* state)
{
  for (int y=0; y<BOARDY; y++)
  {
    for (int x=0; x<BOARDX; x++)
    {
      Cell c = *get_cell(state, x, y);
      if (HAS_COLOR(c.type))
      {
        c.color = rnd(NREALCOLORS); //TODO: random_gem().color;
        set_cell(state, x, y, c);
      }
    }
  }
}

int play_turn(const GameState* state)
{
  if (!ai_choice(state, 0, choose_row, 0, RANGE(0,BOARDY)))
  {
    // TODO: shuffle
    DEBUG("Out of moves, shuffling: %d\n", 0);
    shuffle_board(state);
    find_all_matches(state);
    return play_turn(state);
  } else
    return 1;
}

int grand_finale(const GameState* state)
{
  int score = 0;
  CLEAR_CELLS_WITH(IS_SPECIAL(curcell.type));
  if (score)
  {
    print_board(state);
    move_gems_down(state);
    score += find_all_matches(state);
    ai_add_player_score(ai_current_player(), score);
  }
  return score;
}

void play_game(const GameState* state)
{
  while (state->goaljelly) // TODO: fn
  {
    print_board(state);
    play_turn(state);
  }
  printf("\n*** GOAL REACHED\n");
  while (grand_finale(state))
  {
    print_board(state);
  }
}

void init_tables()
{
  int i = 0;
  for (int y=0; y<BOARDY; y++)
  {
    for (int x=0; x<BOARDX-2; x++)
    {
      BoardMask horiz = bm_or(bm_point(x,y), bm_or(bm_point(x+1,y), bm_point(x+2,y)));
      MASKS3[i++] = horiz;
    }
  }
  for (int x=0; x<BOARDX; x++)
  {
    for (int y=0; y<BOARDY-2; y++)
    {
      BoardMask vert  = bm_or(bm_point(x,y), bm_or(bm_point(x,y+1), bm_point(x,y+2)));
      //printf("#%d %d,%d: %llx %llx\n", i, x, y, vert.h, vert.l);
      MASKS3[i++] = vert;
    }
  }
  assert(i == NMASKS3);
  ADJACENT11 = bm_or(bm_point(1,0), bm_or(bm_point(0,1), bm_or(bm_point(2,1), bm_point(1,2))));
}

void init_game(GameState* state, const char* fname)
{
  memset(state, 0, sizeof(GameState));
  Cell oob = { OUTOFBOUNDS, GCNone };
  for (int i=0; i<sizeof(state->board)/sizeof(Cell); i++)
    state->board[0][i] = oob;
  
  FILE* f = fopen(fname, "r");
  if (!f) abort();
  
  char buf[80];
  for (int y=0; y<BOARDY; y++)
  {
    fgets(buf, 80, f);
    for (int x=0; x<BOARDX; x++)
    {
      Cell c = { };
      c.type = strchr(GEM_TYPES, buf[x])-GEM_TYPES;
      if (HAS_COLOR(c.type))
        c.color = rnd(6);
      else if (c.type == BOMB)
        c.color = GCWildcard;
      else
        c.color = GCNone;
      set_cell(state, x, y, c);
    }
  }
  // find initial matches
  find_all_matches(state);
}

int main(int argc, char** argv)
{
  assert(sizeof(Cell) == 1);
  
  int argi = ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 1;
  defaults.max_search_level = 3*1;
  defaults.max_walk_level = BOARDX*BOARDY;
  ai_init(&defaults);

  GameState state;
  init_tables();
  init_game(&state, "level23.txt");
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
