
#ifndef _CARDS_H
#define _CARDS_H

/**
 * Simple card library, not nearly as good as:
 * http://www.codingthewheel.com/archives/a-pokersource-poker-eval-primer
 */

#include "ai.h"

typedef enum { Red, Black, NUM_CARD_COLORS } CardColor;
typedef enum { Hearts, Diamonds, Clubs, Spades, NUM_CARD_SUITS } CardSuit;
typedef enum { NORANK, Ace, R2, R3, R4, R5, R6, R7, R8, R9, R10, Jack, Queen, King, NUM_CARD_RANKS } CardRank;

typedef uint8_t CardIndex;
typedef uint64_t UnorderedDeck;

#define FIRST_CARD 4
#define DECK52 ((1ull << 54)-1)

inline CardIndex CARD(CardRank rank, CardSuit suit)
{
  return suit | (rank<<2);
}
inline CardSuit SUIT(CardIndex card)
{
  return card&3;
}
inline CardSuit RANK(CardIndex card)
{
  return card>>2;
}
inline CardColor COLOR(CardIndex card)
{
  return SUIT(card)>>1;
}
//#define MAKECARD(rank,suit) (((rank)<<2)|((suit)&3))

const char* get_card_name(CardIndex card);

#endif
