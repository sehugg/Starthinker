
#include "cards.h"

static const char* CARD_SUIT_NAMES[] = { "h", "d", "C", "S" };
static const char* CARD_RANK_NAMES[] = { "-","A","2","3","4","5","6","7","8","9","10","J","Q","K" };

const char* get_card_name(CardIndex card)
{
  static char buf[32];
  if (card)
    sprintf(buf, "%2s%s", CARD_RANK_NAMES[RANK(card)], CARD_SUIT_NAMES[SUIT(card)]);
  else
    sprintf(buf, "   ");
  return buf;
}

