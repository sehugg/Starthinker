
#include "ai.h"

static AIEngineParams defaults = {};

static int max_search_level = 0;
static int default_search_level = 0;
static int max_allocated_search_level = 100;
static int max_walk_level = 0;
static int preliminary_search_inc = 0; // TODO

static bool print_search_stats = false;

int search_level = 0;
int walk_level = 0;
int num_players = 0;

static AIMode ai_mode = AI_UNKNOWN;

bool journal_state = true; // false = not journaling
bool full_search = false; // full search = no beta cutoffs
bool reorder_siblings = true; // killer move heuristic
unsigned int expansion_start_lineno = -1;

static int current_player;
static int seeking_player;

static int score_at_search_start;
static int score_at_walk_start;

static SearchStats* level_stats = NULL;

typedef struct PlayerState
{
  int current_score;
} PlayerState;

typedef struct NodeParams
{
  int alphamax;
  int betamin;
} NodeParams;

typedef struct NodeResult 
{
  int score;
} NodeResult;

static PlayerSettings player_settings[MAX_PLAYERS] = {};

static PlayerState player_state[MAX_PLAYERS] = {};

static int best_modified_score;
static ChoiceIndex* choice_seq;
static int choice_seq_top;
static int choice_seq_transition;
static ChoiceIndex* best_choice_seq;
static int best_choice_seq_top;
static int best_choice_seq_next;

static NodeParams search_params;
static NodeResult search_result;

//

static HashCode random_seed = 0;

// TODO: faster memcpy()

#define DEFAULT_HASH_ORDER 22

typedef enum 
{
  NODE_OPEN,
  NODE_UPPER,
  NODE_EXACT,
  NODE_LOWER,
  NODE_NO_VALID_MOVES,
} NodeType;

static char* NODE_TYPE_NAMES[] = { "open", "upper", "exact", "lower", "invalid" };

typedef struct MemoizedResult
{
  // TODO: pack
  HashCode hash;
  NodeResult result;
  uint8_t depth;
  uint8_t type;
  int8_t bestchoices[2];
} MemoizedResult;

static int max_visited_states = 0;
static MemoizedResult* memoized_results[MAX_PLAYERS];
static MemoizedResult* last_memoized_result;
static MemoizedResult sentinel_memoized_result;
static int memoized_xor = 0;

SearchStats get_cumulative_search_stats()
{
  SearchStats sum = {};
  for (int i=0; i<=max_search_level; i++)
  {
    SearchStats* stats = &level_stats[i];
    sum.visits += stats->visits;
    sum.revisits += stats->revisits;
    sum.cutoffs += stats->cutoffs;
    // TODO: other stats?
  }
  return sum;
}

// use two hashes because index is redundant
bool is_state_visited(HashCode hash, HashCode hash2, MemoizedResult** index)
{
  if (max_visited_states > 0)
  {
    int i = hash & max_visited_states;
    hash ^= hash2 ^ memoized_xor; // after we've computed hash bucket
    MemoizedResult* result = &memoized_results[seeking_player][i];
    /*
    // existing bucket has higher priority?
    int old_sl = result->hash & 0xff;
    if (old_sl < search_level && old_sl != 0)
    {
      *index = &_sentinel_memoized_result;
      return false;
    }
    // we use bottom 8 bits for search level (also acts as hash priority)
    hash = (hash & ~0xff) ^ search_level;
    */
    *index = result;
    return (result->hash == hash);
  } else {
    return false;
  }
}

unsigned int rnd_next()
{
  return random();
/*
  // if in random walk mode, use the fast hash
  if (ai_mode == AI_RANDOM)
  {
    random_seed = compute_hash(&random_seed, sizeof(random_seed), random_seed);
  } else {
    SETGLOBAL(random_seed, current_hash ^ search_level);
  }
  //DEBUG("rnd %x\n", random_seed);
  return random_seed;
*/
}

int choose_bit(ChoiceMask mask, unsigned int r, int nbits)
{
  if (nbits == 1)
    return 0;
    
  int i = r & (nbits-1);
  if (mask & CHOICE(i))
    return i;
    
  nbits >>= 1;
  ChoiceMask rf0 = (mask & (CHOICE(nbits)-1));
  ChoiceMask rf1 = (mask >> nbits);
  //printf("%llx %llx %llx %d %x\n", mask, rf0, rf1, nbits, r);
  if (rf0 > rf1)
    return choose_bit(rf0, r >> 6, nbits);
  else
    return choose_bit(rf1, r >> 6, nbits) + nbits;
}

int rnd_from_mask(ChoiceMask mask)
{
  assert(mask!=0);

  int nbits = sizeof(mask)*8;
  unsigned int r = rnd_next();
  int result = choose_bit(mask, r, nbits);
  assert(mask & CHOICE(result));
  //printf("%llx(%x) -> %d\n", mask, r, result);
  return result;
}

#define MCOPY(T,dst,src) *((T*)dst) = *((T*)src)
static void memcpyfast(void* dst, const void* src, int size)
{
  switch (size)
  {
    case 1: MCOPY(char,dst,src); break;
    case 2: MCOPY(short,dst,src); break;
    case 3: memcpy(dst,src,size); break;
    case 4: MCOPY(int,dst,src); break;
    default: memcpy(dst,src,size); break;
  }
}

int get_modified_score(int player)
{
  //DEBUG("modscore %d %d\n", player_state[0].current_score, player_state[1].current_score);
  int score = player_state[player].current_score;
  for (int i=0; i<num_players; i++)
  {
    if (player != i)
      score -= player_state[i].current_score;
  }
  return score;
}

int ai_get_winning_players()
{
  if (num_players == 1)
    return 0;

  int bestscore = player_state[0].current_score;
  int bestplayer = 0;
  int drawmask = 1;
  for (int i=1; i<num_players; i++)
  {
    int score = player_state[i].current_score;
    if (score == bestscore)
    {
      drawmask |= (1<<i);
    }
    else if (score > bestscore)
    {
      bestscore = score;
      bestplayer = i;
      drawmask = 0;
    }
    else
    {
      drawmask = 0;
    }
  }
  return (drawmask != 0) ? -drawmask : bestplayer;
}

static void ai_update_node_score()
{
  search_result.score = get_modified_score(seeking_player);
  // subtract a penalty the further out in the horizon
  /*
  int level = search_level + walk_level;
  if (level)
  {
    if (search_result.score > level)
      search_result.score -= level;
    else if (search_result.score < -level)
      search_result.score += level;
    else
      search_result.score = 0;
  }
  */
}

void ai_game_over()
{
  SearchStats* stats = &level_stats[search_level];
  int winners = ai_get_winning_players();
  if (winners >= 0)
  {
    stats->wins[winners]++;
    DEBUG("ai_game_over: Player %d won\n", winners);
  }
  else
  {
    stats->draws++;
    DEBUG("ai_game_over: Tied = 0x%x\n", -winners);
  }
  ai_update_node_score();
}

bool ai_set_mode_search(bool research);

bool ai_set_mode_play();

ChoiceIndex ai_next_choice(const void* state, ChoiceFunction fn_move)
{
  assert(ai_mode == AI_PLAY);
  // TODO: sometimes this hits when we use quiescence
  assert(best_choice_seq_next < best_choice_seq_top);

  int choice = best_choice_seq[best_choice_seq_next++];
  DEBUG("ai_next_choice: P%d choice #%d = %d\n", current_player, best_choice_seq_next-1, choice);
  return choice;
}

void ai_keep_best_score()
{
  // save score?
  // TODO: if next player has no valid moves, this doesn't work
  DEBUG("score result = %d (seq len = %d)\n", search_result.score, choice_seq_transition);
  if (choice_seq_transition > 0)
  {
    int score = search_result.score;
    DEBUG("First move, score = %d vs %d\n", score, best_modified_score);
    if (score > best_modified_score)
    {
      best_modified_score = score;
      int n = choice_seq_transition;
      memcpy(best_choice_seq, choice_seq, sizeof(ChoiceIndex)*n);
      best_choice_seq_next = 0; //TODO: move somewhere else?
      best_choice_seq_top = n;
      if (verbose)
      {
        DEBUG("*** Best score = %d (seq", score);
        for (int i=0; i<n; i++)
          DEBUG2(" %d", best_choice_seq[i]);
        DEBUG2("%s)\n","");
      }
    }
  }
}

// transition across deterministic to non-deterministic boundary
// triggered by player change, choice node, or hidden information boundary
// (or at end of moves)
bool ai_transition()
{
  if (ai_mode >= AI_SEARCH)
  {
    if (choice_seq_transition < 0)
    {
      SETGLOBAL(choice_seq_transition, choice_seq_top);
      DEBUG("ai_transition: %d choices\n", choice_seq_top);
    }
    return true;
  } else {
    return false;
  }
}

void ai_update_console_stats()
{
  if (!verbose && search_level > 0)
  {
    static int seq = 0;
    if ((seq & 0x7fff) == 0)
    {
      SearchStats stats = get_cumulative_search_stats();
      if (stats.visits == 0)
        return;
      fprintf(stderr, "[Ply %2d, visited = %7"PRIu64"k, %2d%% memoized, %2d%% cutoff, best = %9d]\r",
        search_level,
        stats.visits/1000,
        (int)(stats.revisits*100/(stats.visits+stats.revisits)),
        (int)(stats.cutoffs*100/stats.visits),
        best_modified_score);
    }
    seq++;
  }
}

/* TODO
void ai_update_win_stats(SearchStats* stats)
{
  int winners = ai_get_winning_players();
  if (winners >= 0)
  {
    //printf("win_stats: %d %d\n", player_state[0].current_score, player_state[1].current_score);
    stats->advantage[winners]++;
  } else {
    stats->draws++;
  }
}
*/

int ai_make_valid_random_move(const void* state, ChoiceFunction fn_move, int rangestart, ChoiceMask rangeflags)
{
  // TODO: make this faster
  do {
    // choose a move at random from the move mask
    // TODO: we don't really need to journal this RNG
    int i = rnd_from_mask(rangeflags);
    // valid move? we're done
    int jtop = jbuffer_top;
    if (fn_move(state, rangestart + i))
      return 1;
      
    assert(journal_state); // we must be journaling if moves fail
    // move was not valid, so we remove that bit
    ChoiceMask bit = CHOICE(i);
    rangeflags ^= bit;
    DEBUG("random move failed, new mask = %"PRIx64"\n", rangeflags);
    // roll back any modifications that were made
    rollback_journal(jtop);
  } while (rangeflags);
  // we went through all the moves and none were valid
  DEBUG("%s: No valid choices\n", "ai_make_valid_random_move");
  return 0;
}

int ai_chance(const void* state, int state_size, ChoiceFunction fn_move, int rangestart, int rangecount)
{
  return ai_choice_ex(state, state_size, fn_move, rangestart, RANGE(0,rangecount-1), AI_OPTION_CHANCE, NULL);
}

static void mark_best_choice(MemoizedResult* memo, int index)
{
  if (index == memo->bestchoices[0])
    return;
  DEBUG("Mark best choice %d\n", index);
  if (index == memo->bestchoices[1])
  {
    SWAP(memo->bestchoices[0], memo->bestchoices[1]);
    return;
  }
  memo->bestchoices[1] = memo->bestchoices[0];
  memo->bestchoices[0] = index;
}

static int cmp_int(const void *pa,const void *pb) 
{
  const int* a = pa;
  const int* b = pb;
  if (*a==*b)
    return 0;
  else
    if (*a < *b)
      return 1;
     else
      return -1;
}

int ai_choice_ex(const void* state, int state_size, ChoiceFunction fn_move, int rangestart, ChoiceMask rangeflags,
  int options, const ChoiceParams* params)
{
  assert(rangeflags);
  assert(search_level <= max_allocated_search_level);

  // if next choice is chance, transition and make random move (unless in search mode)
  if (options & AI_OPTION_CHANCE)
  {
    ai_transition();
    if (ai_mode != AI_SEARCH)
    {
      return ai_make_valid_random_move(state, fn_move, rangestart, rangeflags);
    }
  }
  
  // TODO: this could be a function pointer
  switch (ai_mode)
  {
    case AI_INTERACTIVE:
    {
      // TODO: use rangeflags
      assert(player_settings[current_player].pifunc);
      return fn_move(state, player_settings[current_player].pifunc(state, current_player, fn_move));
    }

    case AI_PLAY:
    {
      if (best_choice_seq_top == best_choice_seq_next) // TODO??
      {
        DEBUG("ai_choice: no next choice as P%d (top=%d)\n", current_player, best_choice_seq_top);
        ai_set_mode_search(false);
        if (preliminary_search_inc) //TODO
        {
          for (int l=preliminary_search_inc; l<max_search_level; l += preliminary_search_inc)
          {
            max_search_level = l;
            DEBUG("Preliminary search @ level %d\n", max_search_level);
            ai_choice_ex(state, state_size, fn_move, rangestart, rangeflags, options, params);
            DEBUG("Preliminary search complete, score = %d\n", search_result.score);
            ai_print_stats();
            ai_set_mode_search(true);
          }
        }
        if (!ai_choice_ex(state, state_size, fn_move, rangestart, rangeflags, options, params))
        {
          DEBUG("ai_choice: no valid choices %d\n", 0);
          ai_set_mode_play();
          best_choice_seq_top = best_choice_seq_next = 0; // TODO?? why don't we do this normally?
          return 0;
        }
        // TODO: check to make sure hash ends up same way when moves are complete?
        DEBUG("ai_choice: got %d best choices\n", best_choice_seq_top);
        ai_print_stats(); // TODO: Printing twice?
        ai_set_mode_play();
      }
      int res = fn_move(state, ai_next_choice(state, fn_move));
      // move must succeed or we did something wrong
      // (this can happen if sim is not completely deterministic -- e.g. using random numbers)
      assert(res); 
      return res;
    }
    
    case AI_RANDOM:
    {
      /*
      int score = get_modified_score(seeking_player);
      if (score != score_at_walk_start)
      {
        DEBUG("Walk score changed by %+d\n", score - score_at_walk_start);
        return 1;
      }
      */
      if (walk_level++ < max_walk_level)
      {
        return ai_make_valid_random_move(state, fn_move, rangestart, rangeflags);
      }
      else
      {
        DEBUG("Walk level exceeded (%d)\n", max_walk_level);
        return 1;
      }
    }
    
    case AI_SEARCH:
      break;
      
    default: assert(0);
  }

  // save journal position
  int jtop = jbuffer_top;
  // too many levels? do random search of rest of game
  // TODO: cutoff?
  SearchStats* stats = &level_stats[search_level];
  TAKEMIN(stats->min_beta,  search_params.betamin);
  TAKEMAX(stats->max_alpha, search_params.alphamax);
  if (search_level >= max_search_level)
  {
    if (max_walk_level <= 0)
    {
      ai_update_node_score(); // TODO: what if we already did this recently?
      return 1;
    }
    DEBUG("Random walk (level %d)\n", search_level);
    assert(search_level == max_search_level);
    debug_level++;
    //assert(num_player_transitions>0); // TODO: what if we don't?
    // TODO: can we save the state if individual moves can be rolled back?
    bool old_journal_state = journal_state;
    if (state_size)
    {
      ai_journal_save(state, state_size);
      journal_state = false; // TODO: dynamically decide whole state vs. journaling?
    }
    stats->visits++;
    ai_mode = AI_RANDOM;
    HashCode oldrandom = random_seed;

    //score_at_walk_start = get_modified_score(seeking_player);
    int result = ai_choice_ex(state, state_size, fn_move, rangestart, rangeflags, options, params); // TODO: do we have to recurse?
    assert(journal_state || result); // we must be journaling if moves fail

    debug_level--;
    DEBUG("Done with random walk (buf %d to %d, result = %d)\n", jtop, jbuffer_top, result);
    ai_update_node_score(); // TODO: what if result == 0? what if we already did this recently?
    //ai_update_win_stats(stats);
    random_seed = oldrandom;
    rollback_journal(jtop);
    ai_mode = AI_SEARCH;
    walk_level = 0;
    journal_state = old_journal_state;
    assert(search_level == max_search_level);
    return result;
  }
  else
  {
    bool first_move = choice_seq_transition < 0;
    // update visited states
    // use ALL THE PARAMS as part of the hash key
    MemoizedResult* memoized = &sentinel_memoized_result;
    //HashCode key = current_hash ^ ((HashCode)rangeflags) ^ ((HashCode)(rangeflags>>32)) ^ (intptr_t)fn_move;
    const HashCode hash1 = current_hash;
    const HashCode hash2 = compute_hash(&rangeflags, sizeof(rangeflags), hash1 + rangestart + (intptr_t)fn_move - (intptr_t)ai_choice_ex);
    if (hash1 == hash2) fprintf(stderr, "\n*** HASH COLLISION %x\n", hash1); // TODO?
    //DEBUG("(%x %llx) => %x\n", current_hash, rangeflags, key);
    stats->visits++;
    bool is_max = current_player == seeking_player;
    int depth = max_search_level - search_level;
    // don't memoize moves before first player transition
    // also, make sure the memoized node is at the same or shallower level (TODO: does this work without first_move?)
    if (best_choice_seq_top > 0 && /*!first_move && */is_state_visited(hash1, hash2, &memoized))
    {
      // don't use memoized values if memoized node depth is shallower than our depth,
      // but we still use the bestchoices[] array
      if (memoized->depth >= depth)
      {
        DEBUG("node visited (%s): %x %x\n", NODE_TYPE_NAMES[memoized->type], hash1, hash2);
        last_memoized_result = memoized;
        switch (memoized->type)
        {
          case NODE_NO_VALID_MOVES:
            stats->revisits++;
            DEBUG("node had %d valid moves\n", 0);
            return 0;
          case NODE_OPEN:
          case NODE_EXACT:
            stats->revisits++;
            search_result = memoized->result;
            DEBUG("node exact value = %d\n", search_result.score);
            return 1;
          case NODE_UPPER:
            if (memoized->result.score <= search_params.alphamax)
            {
              stats->revisits++;
              search_result.score = search_params.alphamax;
              DEBUG("node cutoff, upper bound = %d\n", search_result.score);
              return 1;
            }
            break;
          case NODE_LOWER:
            if (memoized->result.score >= search_params.betamin)
            {
              stats->revisits++;
              search_result.score = search_params.betamin;
              DEBUG("node cutoff, lower bound = %d\n", search_result.score);
              return 1;
            }
            break;
        }
      }
    } else {
      // reset best choices
      memoized->bestchoices[0] = -1;
      memoized->bestchoices[1] = -1;
    }
    assert(memoized);
    memoized->hash = hash1 ^ hash2 ^ memoized_xor;
    memoized->type = NODE_OPEN;
    memoized->result = search_result;
    memoized->depth = depth;

    ai_update_console_stats();

    NodeParams oldparams = search_params;
    NodeParams node = search_params;
    int best_pv_score = is_max ? MIN_SCORE*MAX_PLAYERS : MAX_SCORE*MAX_PLAYERS;
    int total = 0;
    int nchoices = 0;
    float denom = 0;
    // TODO
    if (options & AI_OPTION_CHANCE)
    {
      search_params.alphamax = MIN_SCORE*MAX_PLAYERS;
      search_params.betamin = MAX_SCORE*MAX_PLAYERS;
    }
    // iterate twice: first for most recently cutoff, second for the rest
    ChoiceMask cutoffs = stats->heuristics.best_choices;
    int choice_scores[64];
    int j = memoized->bestchoices[0] >= 0 ? 0 : 2;
    for (; j<4; j++)
    {
      int index;
      ChoiceMask choices;
      // Move ordering
      switch (j)
      {
        // 1. bestchoices[] node list (0-2 values)
        case 0:
        case 1:
          index = memoized->bestchoices[j];
          // make sure this choice is valid
          if (index >= 0 && (rangeflags & CHOICE(index)) != 0)
          {
            choices = 1;
            rangeflags &= ~CHOICE(index);
          }
          else
            choices = 0;
          break;
        // 2. killer move flags
        case 2:
          index = 0;
          choices = rangeflags & cutoffs;
          break;
        // 3. the leftovers
        case 3:
          index = 0;
          choices = rangeflags & ~cutoffs;
          break;
      }
      if (choices) { DEBUG("choice flags #%d = %d + %"PRIx64"\n", j, index, choices); }
      while (choices)
      {
        if (choices & 1)
        {
          if (!(options & AI_OPTION_CHANCE))
          {
            choice_seq[choice_seq_top++] = rangestart + index;
          }
          DEBUG("> choice %d[%d], alpha = %d, beta = %d\n", choice_seq_top-1, rangestart + index, search_params.alphamax, search_params.betamin);
          debug_level++;
          search_level++;
          
          // make move and possibly recurse
          if (!journal_state) // TODO: haven't tested this
            ai_journal_save(state, state_size);
          
          if (fn_move(state, rangestart + index))
          {
            int score = search_result.score;
            ai_transition(); // in case we exited without setting it
            // TODO: how to evaluate chance nodes? http://books.google.com/books?id=UrhlE15k30sC&pg=PA39&lpg=PA39&dq=alpha+beta+search+chance+nodes&source=bl&ots=N0GlFFcH3l&sig=Sypoa0RdTyfMvxQ1E8pPDx2fqvc&hl=en&sa=X&ei=5ukoUb6FA4Ha8AS2v4DwCw&ved=0CDAQ6AEwAA#v=onepage&q=alpha%20beta%20search%20chance%20nodes&f=false
            if (!(options & AI_OPTION_CHANCE))
            {
              total += score;
              // raise alpha?
              if (is_max && score > node.alphamax)
              {
                search_params.alphamax = node.alphamax = score;
                // when raising alpha across first move boundary, record best score + sequence
                if (first_move)
                  ai_keep_best_score();
                // when raising alpha, we want to revisit this move again
                //TODO? stats->heuristics.best_choices |= CHOICE(index);
                //mark_best_choice(memoized, index);
                DEBUG("node %d[%d]: alpha = %d\n", choice_seq_top-1, rangestart + index, node.alphamax);
              }
              // lower beta?
              if (!is_max && score < node.betamin)
              {
                search_params.betamin = node.betamin = score;
                //stats->heuristics.best_choices |= CHOICE(index);
                //mark_best_choice(memoized, index);
                DEBUG("node %d[%d]: beta = %d\n", choice_seq_top-1, rangestart + index, node.betamin);
              }
              //DEBUG("score = %d, alpha = %d, beta = %d\n", score, node.alphamax, node.betamin);
              // TODO: this right?
            } else {
              // weight nodes by probabilities (if available) or average over uniform distribution
              // TODO: update alpha beta?
              if (params && params->probabilities)
              {
                DEBUG("node prob score = %d * %f\n", score, params->probabilities[index]);
                total += score * params->probabilities[index];
                denom += params->probabilities[index];
              } else {
                total += score;
                denom += 1;
              }
            }
            // save this score
            DEBUG("< choice %d[%d] = %d\n", choice_seq_top-1, rangestart + index, score);
            choice_scores[nchoices] = (score << 6) | index; // 0 <= index <= 63
            nchoices++;
          }

          // did we make any changes?
          if (jbuffer_top > jtop)
          {
            //ai_update_win_stats(stats);
            // rollback journal to pre-loop
            rollback_journal(jtop);
          }
          
          search_level--;
          debug_level--;
          if (!(options & AI_OPTION_CHANCE))
          {
            choice_seq_top--;
          }

          // TODO: search all moves if game over?
          // TODO: different modes
          // TODO: AB cutoff optimal move ordering
          if (node.betamin <= node.alphamax && !full_search)
          {
            DEBUG("%s node cutoff @ %d (%d <= %d)\n", is_max?"max":"min", rangestart + index, node.betamin, node.alphamax);
            mark_best_choice(memoized, index);
            if (reorder_siblings)
              stats->heuristics.best_choices |= CHOICE(index); // save this move as recently cutoff (killer heuristic)
            stats->cutoffs++;
            if (j == 0 && nchoices == 1)
              stats->early_cutoffs++;
            // if cutoff, return beta (for max) or alpha (for min)
            if (is_max)
              search_result.score = node.betamin;
            else
              search_result.score = node.alphamax;
            // we're a Cut node
            memoized->type = is_max ? NODE_LOWER : NODE_UPPER;
            goto cutoff;
          }
        }
        choices >>= 1;
        index++;
      }
    }
    //assert(cutoffs == stats->heuristics.best_choices); // should not change because search levels are non-reentrant
    stats->heuristics.best_choices &= ~rangeflags; // no cutoff, so reset recent cutoffs list
    // choose if this is an Exact or All node
    // did we improve alpha? (or beta, if min)
    if (node.alphamax > oldparams.alphamax || node.betamin < oldparams.betamin)
      memoized->type = NODE_EXACT;
    else
      memoized->type = is_max ? NODE_UPPER : NODE_LOWER;
    // score = alpha (max) or beta (min)
    if (is_max)
      search_result.score = node.alphamax;
    else
      search_result.score = node.betamin;
    // sort best moves
    if (nchoices >= 3 && is_max) // TODO: min too?
    {
      DEBUG("Sorting %d scores\n", nchoices);
      qsort(choice_scores, nchoices, sizeof(int), cmp_int);
      mark_best_choice(memoized, choice_scores[1] & 63);
      mark_best_choice(memoized, choice_scores[0] & 63);
    }
cutoff:
    if (nchoices)
    {
      level_stats[search_level+1].choices += nchoices;
      if (denom == 0)
        denom = nchoices;
      // is this a leaf or chance node?
      if (options & AI_OPTION_CHANCE)
      {
        search_result.score = total/denom; // average
        memoized->type = NODE_EXACT;
      }
      //ai_keep_best_score();
      memoized->result = search_result;
      // TODO: what if we had 0 cutoffs?
      DEBUG("player %d, score = %d (alpha = %d, beta = %d)\n", current_player, search_result.score, node.alphamax, node.betamin);
    }
    else 
    {
      // no valid moves (all returned 0)
      memoized->depth = 255; // if no valid moves, we completed the full search
      memoized->type = NODE_NO_VALID_MOVES;
    }
    // restore old search params
    search_params = oldparams;
    DEBUG("node memoized: %x = %d (%s)\n", memoized->hash, memoized->result.score, NODE_TYPE_NAMES[memoized->type]);
    return nchoices > 0;
  }
}

int ai_get_player_score(int player)
{
  assert(player>=0 && player<=num_players);
  return player_state[player].current_score;
}

void ai_set_player_score(int player, int score)
{
  assert(player>=0 && player<num_players);
  PlayerState* plyr = &player_state[player];
  if (score != plyr->current_score)
  {
    SETGLOBAL(plyr->current_score, score);
  }
  ai_update_node_score(); // TODO: redundant?
  DEBUG("ai_score_player(%d/%d) %d -> %d\n",
    player, seeking_player, score, search_result.score);
}

void ai_add_player_score(int player, int addscore)
{
  if (addscore)
    ai_set_player_score(player, ai_get_player_score(player) + addscore);
}

int ai_process_args(int argc, char** argv)
{
  assert(num_players == 0);

#define APPLY_PLAYERS(name,stmt) { for (int i=0; i<MAX_PLAYERS; i++) { if (players & (1<<i)) { stmt; DEBUG("Setting %s for P%d\n", name, i); } } }

  int players = (1<<MAX_PLAYERS)-1;
  char c;
  int v;
  // TODO: help
  while ((c = getopt (argc, argv, "vs0123AFr:d:i:w:H:L:")) != -1)
  {
    switch (c)
    {
      case 'v':
        verbose++;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
        players = (1 << (c-'0'));
        break;
      case 'A':
        players = (1<<MAX_PLAYERS)-1;
        break;
      case 's':
        print_search_stats = true;
        break;
      case 'd':
        v = atoi(optarg);
        APPLY_PLAYERS( "depth", player_settings[i].max_search_depth = v )
        break;
      case 'H':
        max_visited_states = atoi(optarg);
        if (max_visited_states > 0)
          max_visited_states = (1 << max_visited_states) - 1;
        break;
      case 'r':
        random_seed = atoi(optarg);
        break;
      case 'w':
        max_walk_level = atoi(optarg);
        break;
      case 'i':
        preliminary_search_inc = atoi(optarg);
        break;
      case 'F':
        full_search = true;
        break;
      case 'L':
        expansion_start_lineno = atoi(optarg)-1;
        break;
      default:
        assert(0);
        break;
    }
  }
  return optind;
}

bool ai_set_current_player(int player);

// TODO: max_search_level should be default, not max
void ai_init(const AIEngineParams* params)
{
  defaults = *params;

  if (!num_players) num_players = params->num_players;
  if (!default_search_level) default_search_level = params->max_search_level;
  if (default_search_level > max_allocated_search_level)
    max_allocated_search_level = default_search_level;
  if (!max_walk_level) max_walk_level = params->max_walk_level;
  if (!max_visited_states) max_visited_states = (1 << params->hash_table_order) - 1;

  // TODO: defaults?
  // TODO: min and max players
  if (!num_players) num_players = 2;
  if (!default_search_level) default_search_level = 10;
  if (!max_walk_level) max_walk_level = -1;
  if (!max_visited_states) max_visited_states = (1 << DEFAULT_HASH_ORDER) - 1;
  
  search_level = 0;
  walk_level = 0;
  current_player = 0;
  seeking_player = 0;
  init_hashing();

  level_stats = (SearchStats*) calloc(max_allocated_search_level+1, sizeof(SearchStats));
  current_hash = 0xFFFFFFFF;
  if (max_visited_states > 0)
  {
    for (int i=0; i<num_players; i++)
      memoized_results[i] = (MemoizedResult*) calloc(max_visited_states+1, sizeof(MemoizedResult));
  }
    
  choice_seq = (ChoiceIndex*) calloc(max_allocated_search_level, sizeof(ChoiceIndex));
  best_choice_seq = (ChoiceIndex*) calloc(max_allocated_search_level, sizeof(ChoiceIndex));
  best_choice_seq_next = best_choice_seq_top = 0;
  
  srandom(random_seed);
  ai_set_current_player(0);
  ai_set_mode_play();
}

PlayerSettings* ai_player_settings(int player)
{
  return &player_settings[player];
}

int ai_current_player()
{
  return current_player;
}

int ai_seeking_player()
{
  return seeking_player;
}

bool ai_set_current_player(int player)
{
  // TODO? this doesnt work for 1 player
  if (player != current_player)
  {
    SETGLOBAL(current_player, player);
    DEBUG("Current player = P%d\n", player);
    return ai_transition();
  } else {
    return ai_is_searching();
  }
}

bool ai_next_player()
{
  // TODO: break at turn boundaries not player boundaries?
  return ai_set_current_player((current_player+1) % num_players);
}

bool ai_set_mode_search(bool research)
{
  commit_journal();
  PlayerSettings* plyr = &player_settings[current_player];
  if (plyr->pifunc != NULL)
  {
    DEBUG("%s: %s\n", "ai_set_mode_search", "interactive mode");
    return false;
  }
  else
  {
    ai_mode = AI_SEARCH;
    seeking_player = current_player;
    max_search_level = player_settings[seeking_player].max_search_depth;
    if (max_search_level == 0 || max_search_level > max_allocated_search_level)
      max_search_level = default_search_level;
    memset(&search_result, 0, sizeof(search_result));
    memset(&search_params, 0, sizeof(search_params));
    search_params.alphamax = MIN_SCORE*MAX_PLAYERS;
    search_params.betamin = MAX_SCORE*MAX_PLAYERS;
    for (int i=0; i<=max_search_level; i++)
    {
      SearchStats* stats = &level_stats[i];
      // only clear heuristics on first iteration
      if (research)
        memset(((void*)stats) + sizeof(SearchHeuristics), 0, sizeof(SearchStats) - sizeof(SearchHeuristics));
      else
        memset(stats, 0, sizeof(SearchStats));
      stats->min_beta = search_params.betamin;
      stats->max_alpha = search_params.alphamax;
    }
    best_modified_score = MIN_SCORE*MAX_PLAYERS;
    choice_seq_transition = -1;
    choice_seq_top = best_choice_seq_next = best_choice_seq_top = 0;
    if (memoized_results != NULL)
    {
      //memoized_xor++; // TODO? this makes us forget old results... hopefully
      //for (int i=0; i<num_players; i++) { memset(memoized_results[i], 0, sizeof(MemoizedResult)*(max_visited_states+1)); }
    }
    //DEBUG("ai_set_mode_search: P%d, %d levels, xor=%x\n", seeking_player, max_search_level, memoized_xor);
    score_at_search_start = get_modified_score(seeking_player);
    sentinel_memoized_result.type = NODE_NO_VALID_MOVES;
    last_memoized_result = &sentinel_memoized_result;
    return true;
  }
}

bool ai_set_mode_play()
{
  // TODO: got moves?
  /*
  if (best_choice_seq_top == 0)
  {
    DEBUG("%s: DRAW\n", "ai_set_mode_play");
    ai_mode = AI_UNKNOWN;
    return false;
  }
  assert(jbuffer_top == 0);
  */
  PlayerSettings* plyr = &player_settings[current_player];
  ai_mode = plyr->pifunc != NULL ? AI_INTERACTIVE : AI_PLAY;
  seeking_player = current_player;
  DEBUG("%s: mode = %s\n", "ai_set_mode_play", plyr->pifunc!=NULL?"interactive":"commit");
  return true;
}

AIMode ai_get_mode()
{
  return ai_mode;
}

bool ai_is_searching()
{
  return (ai_mode >= AI_SEARCH);
}

HashCode ai_current_hash()
{
  return current_hash;
}

//

void ai_print_stats()
{
  if (!print_search_stats) return;
    
  int level;
  SearchStats cumul;
  memset(&cumul, 0, sizeof(SearchStats));
  int lastcumul = 1;
  printf("                 VISITS    MEM    CUT   ECUT     BF MAXALPHA  MINBETA   P1WIN  P2WIN   DRAW          \n\n");
  //      Level  19:       271113    21%    73%    67%    0.6     1390     -510      0%     0%     0%
  for (level=1; level<=max_search_level; level++)
  {
    const SearchStats* stats = &level_stats[level];
    cumul.visits += stats->visits;
    if (stats->visits)
    {
      int pi;
      printf("Level %3d: %12"PRIu64" %5.0f%% %5.0f%% %5.0f%% %6.1f %8d %8d ",
        level,
        stats->visits,
        stats->revisits*100.0/(stats->revisits+stats->visits),
        stats->cutoffs*100.0/stats->visits,
        stats->early_cutoffs*100.0/stats->visits,
        (cumul.visits-lastcumul)*1.0f/lastcumul,
        stats->max_alpha,
        stats->min_beta);
      if (stats->choices)
      {
        for (pi=0; pi<num_players; pi++)
          printf("   %3d%%", (int)(stats->wins[pi] * 100 / stats->choices));
        printf("   %3d%%", (int)(stats->draws * 100 / stats->choices));
      }
      printf("\n");
    }
    lastcumul = cumul.visits;
  }
  fflush(stdout);
}

void ai_print_endgame_results()
{
  int winners = ai_get_winning_players();
  printf("\n\n");
  if (winners >= 0)
  {
    printf("WINNER: Player %d\n", winners);
  } else {
    printf("DRAW: Players 0x%x\n", -winners);
  }
  for (int i=0; i<num_players; i++)
  {
    printf("  Player %d: Score %d\n", i, player_state[i].current_score);
  }
}

// debugging

static unsigned int debug_lineno = 0;

static void _ai_count_linefeeds(const char* fmt)
{
  // count the newlines
  int count = 0;
  char ch;
  while ((ch = *fmt++) != 0)
    if (ch == '\n')
      count++;
  // increment line number
  debug_lineno += count;
  // TODO
  if (debug_lineno >= expansion_start_lineno)
  {
    verbose = 100;
    expansion_start_lineno = -1;
    printf("=== VERBOSITY ON ===\n");
  }
}

int _ai_log(const char* fmt)
{
  _ai_count_linefeeds(fmt);
  //fprintf(stdout, "(%5d:)", debug_lineno);
  if (debug_level)
    fprintf(stdout, "[%2d]%*s", debug_level, debug_level*2-1, " ");
  // for now, always log
  return 1;
}

// override printf function w/ line tracking
int printf(const char *fmt, ...) 
{
  va_list argp;
  _ai_count_linefeeds(fmt);
  va_start(argp, fmt);
  int ret = vfprintf(stdout, fmt, argp);
  va_end(argp);
  return ret;
}
int puts(const char *fmt)
{
  _ai_count_linefeeds(fmt);
  return fputs(fmt, stdout);
}
int putchar(int c)
{
  if (c == '\n') _ai_count_linefeeds("\n");
  return fputc(c, stdout);
}

// TODO: player can forfeit?

