
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
static int GEM_COLORS[8] = { COLOR_RED, COLOR_CYAN, COLOR_YELLOW, COLOR_GREEN, COLOR_BLUE, COLOR_MAGENTA, COLOR_BLACK, COLOR_BLACK };

#define BOARDX 9
#define BOARDY 9
#define PADDING 2

typedef uint32_t RowMask;

typedef struct
{
  GemType type:3;
  GemColor color:3;
} __attribute__((packed)) Cell;

typedef struct
{
  Cell board[BOARDY+PADDING*2][16]; // padding on all sides, so coords are all 2-indexed
  uint8_t jelly[BOARDY][16];
  int16_t cols[NCOLORS][BOARDX];
  int16_t rows[NCOLORS][BOARDY];
} GameState;

//

#define rnd(n) (((unsigned int)random()) % n)

#define GEM_SCORE 1
#define JELLY_SCORE 5
#define STONE_SCORE 16

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
    SET(state->cols[dst->color][x], state->cols[dst->color][x] & ~CHOICE(y));
    SET(state->rows[dst->color][y], state->rows[dst->color][y] & ~CHOICE(x));
  }
  SET(*dst, c);
  if (c.color != GCNone)
  {
    SET(state->cols[c.color][x], state->cols[c.color][x] | CHOICE(y));
    SET(state->rows[c.color][y], state->rows[c.color][y] | CHOICE(x));
  }
}

void print_board(const GameState* state)
{
  printf("\nBOARD:\n\n");
  printf("    A  B  C  D  E  F  G  H  J\n\n");
  int x,y;
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
      if (c.color != GCNone)
      {
        assert(state->cols[c.color][x] & CHOICE(y));
        assert(state->rows[c.color][y] & CHOICE(x));
      }
    }
    printf("\n");
  }
  printf("    A  B  C  D  E  F  G  H  J\n\n");
}

static int move_row = -1;
static int move_col = -1;

void play_turn(const GameState* state);

int remove_stone(const GameState* state, int x0, int y0, int x1, int y1)
{
  int score = 0;
  for (int y=y0; y<=y1; y++)
  {
    for (int x=x0; x<=x1; x++)
    {
      Cell c = *get_cell(state, x, y);
      if (c.type == STONE)
      {
        c.type = BLANK;
        SET(state->jelly[y][x], 2);
        set_cell(state, x, y, c);
        score += STONE_SCORE;
      }
    }
  }
  DEBUG("remove_stone(%d,%d - %d,%d): %d\n", x0, y0, x1, y1, score);
  return score;
}

int remove_gems(const GameState* state, int n, int x0, int y0, int dx, int dy)
{
  int x = x0;
  int y = y0;
  int score = n * GEM_SCORE;
  DEBUG("remove %d gems (%d,%d) +(%d,%d)\n", n, x, y, dx, dy);
  Cell c = { };
  for (int i=0; i<n; i++)
  {
    int j = state->jelly[y][x];
    if (j)
    {
      DEC(state->jelly[y][x]);
      score += JELLY_SCORE;
    }
    set_cell(state, x, y, c);
    x += dx;
    y += dy;
  }
  // remove stone
  if (dx)
    score += remove_stone(state, x0-1, y0-1, x0+dx*n, y0+1);
  else
    score += remove_stone(state, x0-1, y0-1, x0+1, y0+dy*n);
  return score;
}

int find_col_match(const GameState* state, int flags, int col, int n)
{
  int total = 0;
  int mask = MASK(n);
  for (int i=0; flags && i<BOARDX-n; i++)
  {
    if ((flags & mask) == mask)
    {
      DEBUG("found match %d @ %d,%d\n", n, col, i);
      total += remove_gems(state, n, col, i, 0, 1);
    }
    flags >>= 1;
  }
  return total;
}

int find_row_match(const GameState* state, int flags, int row, int n)
{
  int total = 0;
  int mask = MASK(n);
  for (int i=0; flags && i<BOARDX-n; i++)
  {
    if ((flags & mask) == mask)
    {
      remove_gems(state, n, i, row, 1, 0);
      total += n;
    }
    flags >>= 1;
  }
  return total;
}

Cell random_gem()
{
  return MAKECELL(PLAIN, rnd(6));
}

void move_gems_down(const GameState* state)
{
  for (int x=0; x<BOARDX; x++)
  {
    int srcy = BOARDY-1;
    int desty = BOARDY-1;
    while (desty >= 0)
    {
      // skip blank cells
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
      }
      desty--;
      srcy--;
    }
  }
}

int find_all_matches(const GameState* state);

int find_matches(const GameState* state, int colorflags, int colflags, int rowflags)
{
  assert(BOARDX==BOARDY);
  int total = 0;
  DEBUG("find_matches(colors %x, cols %x, rows %x)\n", colorflags, colflags, rowflags);
  for (int color=0; color<NREALCOLORS; color++)
  {
    if (CHOICE(color) & colorflags)
    {
      // look for matches in each column
      for (int i=0; i<BOARDX; i++)
      {
        //DEBUG("color %d index %d: col = %x, row = %x\n", color, i, state->cols[color][i], state->rows[color][i]);
        for (int n=5; n>=3; n--)
        {
          if (CHOICE(i) & colflags)
            total += find_col_match(state, state->cols[color][i], i, n);
          if (CHOICE(i) & rowflags)
            total += find_row_match(state, state->rows[color][i], i, n);
        }
      }
    }
  }
  if (total)
  {
    SETGLOBAL(move_row,-1);
    SETGLOBAL(move_col,-1);
  }
  while (total)
  {
    ai_add_player_score(ai_current_player(), total);
    if (!ai_is_searching()) print_board(state);
    move_gems_down(state);
    if (!ai_is_searching()) print_board(state);
    int score = find_all_matches(state);
    if (!score)
      break;
    total += score;
  }
  return total;
}

int find_all_matches(const GameState* state)
{
  return find_matches(state, MASK(NREALCOLORS), MASK(BOARDX), MASK(BOARDY));
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
  if (IS_GEM(src->type) && IS_GEM(dest->type) && src->color != dest->color)
  {
    DEBUG("swap %d,%d with %d,%d\n", move_col, move_row, dest_col, dest_row);
    Cell sc = *src;
    Cell dc = *dest;
    set_cell(state, dest_col, dest_row, sc);
    set_cell(state, move_col, move_row, dc);
    // at least one match?
    return find_matches(state, CHOICE(sc.color)|CHOICE(dc.color), CHOICE(move_col)|CHOICE(dest_col), CHOICE(move_row)|CHOICE(dest_row)) > 0;
  } else
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

void play_turn(const GameState* state)
{
  if (!ai_choice(state, 0, choose_row, 0, RANGE(0,BOARDY)))
  {
    // TODO: shuffle
    printf("Out of moves\n");
  }
}

void play_game(const GameState* state)
{
  while (true)
  {
    print_board(state);
    play_turn(state);
  }
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
      //printf("%d,%d c%d col %x row %x\n", x, y, c.color, state->cols[c.color][x], state->rows[c.color][y]);
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
  defaults.num_players = 2;
  defaults.max_search_level = 10;
  defaults.max_walk_level = BOARDX*BOARDY*2;
  ai_init(&defaults);

  GameState state;
  init_game(&state, "level23.txt");
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
