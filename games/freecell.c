
#include "ai.h"
#include "cards.h"

#define NUM_CELLS 4
#define NUM_CASCADES 8

#define CELL_PENALTY 50
#define FOUNDATION_REWARD 100

typedef struct
{
  int ncards;
  CardIndex cards[52];
} OrderedDeck;

typedef struct
{
  CardIndex cell[NUM_CELLS];
  OrderedDeck foundation[NUM_CARD_SUITS];
  OrderedDeck cascade[NUM_CASCADES];
} GameState;

//

const CardIndex* top_card(const OrderedDeck* deck)
{
  return deck->ncards ? &deck->cards[deck->ncards-1] : NULL;
}

#define COLOR_RED "\033[31m"
#define COLOR_NML "\033[0m"

void print_board(const GameState* state)
{
  printf("\nBOARD:\n\n");
  // print cells
  int i;
  for (i=0; i<NUM_CELLS; i++)
  {
    printf(" [%s] ", get_card_name(state->cell[i]));
  }
  for (i=0; i<NUM_CARD_SUITS; i++)
  {
    printf(" <%s> ", get_card_name(*top_card(&state->foundation[i])));
  }
  int j=0;
  int n;
  do {
    printf("\n");
    n=0;
    for (i=0; i<NUM_CASCADES; i++)
    {
      if (j < state->cascade[i].ncards)
      {
        printf("  %s  ", get_card_name(state->cascade[i].cards[j]));
        n++;
      } else {
        printf("       ");
      }
    }
    j++;
  } while (n);
  printf("\n");
}

inline int rnd(int n)
{
  return (((unsigned int)random()) % n);
}

void init_deck(OrderedDeck* deck)
{
  for (int i=0; i<52; i++)
  {
    deck->cards[i] = FIRST_CARD + 51-i;
  }
  deck->ncards = 52;
}

void shuffle_cards(OrderedDeck* deck)
{
  int n = deck->ncards;
  int i;
  // NOTE: we nerf (skip some steps in the shuffle to make the puzzle solvable)
  // set nerf=1 to disable nerfing
  int nerf=3;
  for (i=n-1; i>1; i--)
  {
    if (nerf>1 && (i%nerf) == 0)
      continue;
    int j = rnd(i-1);
    if (deck->cards[i])
      SWAP(deck->cards[i], deck->cards[j]);
    else
      deck->ncards--;
  }
}

void init_game(GameState* state)
{
  memset(state, 0, sizeof(GameState));
  OrderedDeck deck;
  init_deck(&deck);
  shuffle_cards(&deck);
  // deal to cascades
  for (int i=0; i<deck.ncards; i++)
  {
    int c = (i % NUM_CASCADES);
    OrderedDeck* pile = &state->cascade[c];
    pile->cards[pile->ncards++] = deck.cards[i];
  }
}

bool is_compatible_cascade(const CardIndex src, const OrderedDeck* cascade, bool* useempty)
{
  assert(src);
  assert(cascade);
  const CardIndex* top = top_card(cascade);
  // empty cascade is fine
  if (top == NULL)
  {
    if (*useempty)
    {
      *useempty = false;
      return true;
    }
    else
      return false;
  }
  else // descending alternating colors
    return COLOR(src) != COLOR(*top) && (RANK(src)+1) == RANK(*top);
}

bool is_compatible_foundation(const CardIndex src, const OrderedDeck* foundation)
{
  assert(src);
  assert(foundation);  
  const CardIndex* top = top_card(foundation);
  // Ace low
  if (top == NULL)
    return RANK(src) == Ace;
  else // ascending, same suit
    return SUIT(src) == SUIT(*top) && (RANK(src) == (RANK(*top)+1));
}

bool is_game_over(const GameState* state)
{
  for (int i=0; i<NUM_CARD_SUITS; i++)
    if (state->foundation[i].ncards < 13)
      return false;
  return true; // all foundations have 13 cards
}

bool play_turn(const GameState* state);

static const OrderedDeck* source_deck = 0;
static const CardIndex* source_card = 0;

int move_card(const void* pstate, ChoiceIndex index)
{
  const GameState* state = pstate;

  // what card did we pick for target?
  const OrderedDeck* pdeck;
  const CardIndex* pcard;
  if (index < NUM_CASCADES)
  {
    pdeck = &state->cascade[index];
    pcard = NULL;
    //ai_add_player_score(ai_current_player(), 20); // reward for moving into cascade
    DEBUG("Moving to cascade %d\n", index);
  }
  else if (index < NUM_CASCADES + NUM_CELLS)
  {
    pdeck = NULL;
    pcard = &state->cell[index - NUM_CASCADES];
    ai_add_player_score(ai_current_player(), -CELL_PENALTY); // penalty for moving into cell
    DEBUG("Moving to cell %d\n", index - NUM_CASCADES);
  }
  else if (index < NUM_CASCADES + NUM_CELLS + NUM_CARD_SUITS)
  {
    pdeck = &state->foundation[index - NUM_CASCADES - NUM_CELLS];
    pcard = NULL;
    ai_add_player_score(ai_current_player(), FOUNDATION_REWARD); // reward for moving into foundation
    DEBUG("Moving to foundation %d\n", index - NUM_CASCADES - NUM_CELLS);
  }
  else
    assert(0);
  // pull source card
  CardIndex card;
  if (source_deck)
  {
    // draw from deck
    assert(source_deck->ncards);
    card = *top_card(source_deck);
    DEC(source_deck->ncards);
  }
  else
  {
    // draw from cell
    assert(source_card);
    card = *source_card;
    SET(*source_card, 0); //make empty cell
  }
  // push to target
  if (pdeck)
  {
    assert(!pcard);
    SET(pdeck->cards[pdeck->ncards], card);
    INC(pdeck->ncards);
    assert(pdeck->ncards <= 52);
    // game over?
    if (pdeck->ncards == 13 && is_game_over(state))
    {
      ai_game_over();
      return 1;
    }
  }
  else
  {
    assert(pcard);
    SET(*pcard, card);
  }
  // not really next player .. but next turn
  if (ai_next_player())
  {
    return play_turn(state);
  }
  return 1;
}

int source_to_target(const void* pstate, ChoiceIndex index)
{
  const GameState* state = pstate;
  
  // what card did we pick for source?
  const OrderedDeck* pdeck;
  const CardIndex* pcard;
  if (index < NUM_CASCADES)
  {
    pdeck = &state->cascade[index];
    pcard = top_card(pdeck);
    DEBUG("Choosing from cascade %d: %s\n", index, get_card_name(*pcard));
  }
  else if (index < NUM_CASCADES + NUM_CELLS)
  {
    pdeck = NULL;
    pcard = &state->cell[index - NUM_CASCADES];
    ai_add_player_score(ai_current_player(), CELL_PENALTY); // reward for moving out of cell
    DEBUG("Choosing from cell %d: %s\n", index - NUM_CASCADES, get_card_name(*pcard));
  }
  else if (index < NUM_CASCADES + NUM_CELLS + NUM_CARD_SUITS)
  {
    pdeck = &state->foundation[index - NUM_CASCADES - NUM_CELLS];
    pcard = top_card(pdeck);
    ai_add_player_score(ai_current_player(), -FOUNDATION_REWARD); // penalty for moving out of foundation
    DEBUG("Choosing from foundation %d: %s\n", index - NUM_CASCADES - NUM_CELLS, get_card_name(*pcard));
  }
  else
    assert(0);
    
  assert(pcard);
  SETGLOBAL(source_card, pcard);
  SETGLOBAL(source_deck, pdeck);
  // choose compatible destination
  ChoiceMask mask = 0;
  // target can be cascade...
  bool useempty = true; // only use first available empty slot
  for (int i=0; i<NUM_CASCADES; i++)
    if (is_compatible_cascade(*pcard, &state->cascade[i], &useempty))
      mask |= CHOICE(i);
  // cell...
  if (pdeck) // do not move from cell to cell .. no point
  {
    for (int i=0; i<NUM_CELLS; i++)
      if (state->cell[i] == 0)
      {
        mask |= CHOICE(i+NUM_CASCADES);
        break; // only use first available empty cell
      }
  }
  // or foundation...
  for (int i=0; i<NUM_CARD_SUITS; i++)
    if (SUIT(*pcard) == i && is_compatible_foundation(*pcard, &state->foundation[i])) // foundations are assigned to suits
      mask |= CHOICE(i+NUM_CASCADES+NUM_CELLS);

  if (mask == 0)
    return false;
  else
    return ai_choice(state, 0, move_card, 0, mask);
}

bool play_turn(const GameState* state)
{
  ChoiceMask mask = 0;
  // source can be non-empty cascade...
  for (int i=0; i<NUM_CASCADES; i++)
    if (state->cascade[i].ncards)
      mask |= CHOICE(i);
  // cell...
  for (int i=0; i<NUM_CELLS; i++)
    if (state->cell[i])
      mask |= CHOICE(i+NUM_CASCADES);
  // or foundation...
  for (int i=0; i<NUM_CARD_SUITS; i++)
    if (state->foundation[i].ncards)
      mask |= CHOICE(i+NUM_CASCADES+NUM_CELLS);

  if (mask == 0)
    return false;
  else
    return ai_choice(state, 0, source_to_target, 0, mask);
}

void play_game(const GameState* state)
{
  print_board(state);
  while (play_turn(state) && !is_game_over(state))
  {
    print_board(state);
  }
  printf("\n\n*** GAME OVER\n\n");
  print_board(state);
}

int main(int argc, char** argv)
{
  GameState state;
  assert(NUM_CELLS + NUM_CASCADES + 4 < 64);
  
  ai_process_args(argc,argv);

  AIEngineParams defaults = {};
  defaults.num_players = 1;
  defaults.max_search_level = 30;
  defaults.max_walk_level = -1;
  defaults.hash_table_order = 26;
  ai_init(&defaults);

  init_game(&state);
  play_game(&state);
  ai_print_endgame_results(&state);
  
  return 0;
}
