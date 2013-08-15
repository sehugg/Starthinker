
static const char* EPD_PIECE_CHARS = "<PNBRQK>pnbrqk";

static char* epd_bestmove = 0;

bool epd_char_to_piece_type(char ch, int *player, PieceType *type)
{
  assert(type);
  assert(player);
  char* pt = strchr(EPD_PIECE_CHARS, ch);
  if (!pt)
    return false;

  *type = pt-EPD_PIECE_CHARS;
  *player = 0;
  if (*type >= NUM_PIECE_TYPES)
  {
    *type -= NUM_PIECE_TYPES;
    *player = 1;
  }
  return true;
}

void verify_move(const GameState* oldstate, const GameState* newstate)
{
  char ourmove[16] = {};
  char* p = ourmove;
  I2XY(move_src, x1, y1);
  I2XY(move_dest, x2, y2);
  //DEBUG("%d,%d -> %d,%d\n", x1, y1, x2, y2);
  PieceDef src  = oldstate->board[y1][x1];
  PieceDef dest = newstate->board[y2][x2];
  bool ambig_row = false;
  bool ambig_col = false;
  if (src.type != Pawn)
  {
    *p++ = EPD_PIECE_CHARS[src.type];
  }
  for (int y=0; y<BOARDY; y++)
  {
    for (int x=0; x<BOARDX; x++)
    {
      if (x==x1 && y==y1)
        continue;
      PieceDef p = oldstate->board[y][x];
      if (p.player == src.player && p.type == src.type)
      {
        if (y==y1)
          ambig_row = true;
        if (x==x1)
          ambig_col = true;
      }
    }
  }
  if ((ambig_row && y1==y2) || (src.type==Pawn && x1!=x2))
    *p++ = 'a' + x1;
  if (ambig_col && x1==x2)
    *p++ = '1' + y1;
  // capture move?
  if (oldstate->board[y2][x2].type && oldstate->board[y2][x2].player != dest.player)
    *p++ = 'x';
  //if (x1 != x2)
    *p++ = 'a' + x2;
  //if (y1 != y2)
    *p++ = '1' + y2;
  if (newstate->incheck[src.player^1])
    *p++ = '+';
  printf("Move: %s vs %s match = %d\n", ourmove, epd_bestmove, strstr(epd_bestmove, ourmove) != 0);
}

bool parse_epd(GameState* state, char* str)
{
#define EPDERR(str) { fprintf(stderr, "***EPDERROR: %s\n", str); return false; }

  DEBUG("EPD: Parsing line %s\n", str);
  char* pieces = strsep(&str, " ");
  char* side2move = strsep(&str, " ");
  char* castling = strsep(&str, " ");
  char* enpass = strsep(&str, " ");
  char* operations = strsep(&str, "\n");
  
  if (!pieces || !side2move || !castling || !enpass)
    EPDERR("Bad EPD string");
  
  // parse castling flags  
  int castling_flags[2] = {0,0};
  {
    int p;
    PieceType t;
    char ch;
    while ((ch = *castling++) != 0)
    {
      if (epd_char_to_piece_type(ch, &p, &t))
      {
        castling_flags[p] |= 1 << t;
      }
    }
    DEBUG("EPD: Castling flags %x,%x\n", castling_flags[0], castling_flags[1]);
  }
  
  // piece placement
  for (int y=7; y>=0; y--)
  {
    char* ranki = strsep(&pieces, "/");
    if (!ranki) EPDERR("Not enough ranks");
    DEBUG("EPD: rank %d %s\n", y+1, ranki);
    char ch;
    int x = 0;
    while ((ch = *ranki++) != 0)
    {
      if (x >= BOARDX) EPDERR("X coord out of bounds");
      switch (ch)
      {
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
          x += (ch-'0');
          break;
        default:
        {
          PieceType type;
          int player;
          if (epd_char_to_piece_type(ch, &player, &type))
          {
            PieceDef piece = MAKEPIECE(player,type);
            if (piece.type == King && y==player*7 && x==4)
            {
              piece.moved = (castling_flags[player] & ((1<<King)|(1<<Queen))) == 0;
            }
            if (piece.type == Rook && y==player*7 && (x==0||x==7))
            {
              piece.moved = (castling_flags[player] & ((1<<(x==0?Queen:King)))) == 0;
            }
            else
              piece.moved = true; // just need it for kings and rooks
            set_piece(state, x, y, piece);
            x++;
          } else EPDERR("Invalid piece character");
          break;
        }
      }
    }
    if (x != BOARDX) EPDERR("Did not finish filling rank");
  }
  
  // side to move
  if (!strcmp("w", side2move)) ai_set_current_player(WHITE);
  else if (!strcmp("b", side2move)) ai_set_current_player(BLACK);
  else EPDERR("Side to move invalid");

  if (strcmp("-", enpass))
  {
    DEBUG("ENPASS [%s]\n", enpass);
    if (strlen(enpass)!=2) EPDERR("Bad en passant string");
    int x = enpass[0] - 'a';
    int y = enpass[1] - '1';
    state->enpassant[ai_current_player()^1] = BI(x,y);
  }
  
  // TODO: operations
  epd_bestmove = operations;
  
  return true;
}

bool parse_epd_file(const char* filename)
{
  printf("EPD: Opening '%s'\n", filename);
  FILE* f = fopen(filename, "r");
  if (!f) EPDERR("Could not read file");
  char buf[256];
  int line=0;
  while (fgets(buf, sizeof(buf), f))
  {
    line++;
    GameState state;
    memset(&state, 0, sizeof(state));
    if (!parse_epd(&state, buf))
      return false;

    GameState oldstate = state;
    printf("EPD: Board Initial (%s:%d)\n", filename, line);
    print_board(&state);
    play_turn(&state);
    printf("EPD: Board Best    (%s:%d)\n", filename, line);
    print_board(&state);
    verify_move(&oldstate, &state);
    ai_print_endgame_results(&state);
    if (!verbose) fprintf(stderr, "\n");
  }
  return true;
}
