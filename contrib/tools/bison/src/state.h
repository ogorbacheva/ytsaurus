/* Type definitions for the finite state machine for Bison.

   Copyright (C) 1984, 1989, 2000-2004, 2007, 2009-2015, 2018-2019 Free
   Software Foundation, Inc.

   This file is part of Bison, the GNU Compiler Compiler.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


/* These type definitions are used to represent a nondeterministic
   finite state machine that parses the specified grammar.  This
   information is generated by the function generate_states in the
   file LR0.

   Each state of the machine is described by a set of items --
   particular positions in particular rules -- that are the possible
   places where parsing could continue when the machine is in this
   state.  These symbols at these items are the allowable inputs that
   can follow now.

   A core represents one state.  States are numbered in the NUMBER
   field.  When generate_states is finished, the starting state is
   state 0 and NSTATES is the number of states.  (FIXME: This sentence
   is no longer true: A transition to a state whose state number is
   NSTATES indicates termination.)  All the cores are chained together
   and FIRST_STATE points to the first one (state 0).

   For each state there is a particular symbol which must have been
   the last thing accepted to reach that state.  It is the
   ACCESSING_SYMBOL of the core.

   Each core contains a vector of NITEMS items which are the indices
   in the RITEM vector of the items that are selected in this state.

   The two types of actions are shifts/gotos (push the lookahead token
   and read another/goto to the state designated by a nterm) and
   reductions (combine the last n things on the stack via a rule,
   replace them with the symbol that the rule derives, and leave the
   lookahead token alone).  When the states are generated, these
   actions are represented in two other lists.

   Each transition structure describes the possible transitions out
   of one state, the state whose number is in the number field.  Each
   contains a vector of numbers of the states that transitions can go
   to.  The accessing_symbol fields of those states' cores say what
   kind of input leads to them.

   A transition to state zero should be ignored: conflict resolution
   deletes transitions by having them point to zero.

   Each reductions structure describes the possible reductions at the
   state whose number is in the number field.  rules is an array of
   num rules.  lookahead_tokens is an array of bitsets, one per rule.

   Conflict resolution can decide that certain tokens in certain
   states should explicitly be errors (for implementing %nonassoc).
   For each state, the tokens that are errors for this reason are
   recorded in an errs structure, which holds the token numbers.

   There is at least one goto transition present in state zero.  It
   leads to a next-to-final state whose accessing_symbol is the
   grammar's start symbol.  The next-to-final state has one shift to
   the final state, whose accessing_symbol is zero (end of input).
   The final state has one shift, which goes to the termination state.
   The reason for the extra state at the end is to placate the
   parser's strategy of making all decisions one token ahead of its
   actions.  */

#ifndef STATE_H_
# define STATE_H_

# include <stdbool.h>

# include <bitset.h>

# include "gram.h"
# include "symtab.h"


/*-------------------.
| Numbering states.  |
`-------------------*/

typedef int state_number;
# define STATE_NUMBER_MAXIMUM INT_MAX

/* Be ready to map a state_number to an int.  */
static inline int
state_number_as_int (state_number s)
{
  return s;
}


typedef struct state state;

/*--------------.
| Transitions.  |
`--------------*/

typedef struct
{
  int num;
  state *states[1];
} transitions;


/* What is the symbol labelling the transition to
   TRANSITIONS->states[Num]?  Can be a token (amongst which the error
   token), or nonterminals in case of gotos.  */

# define TRANSITION_SYMBOL(Transitions, Num) \
  (Transitions->states[Num]->accessing_symbol)

/* Is the TRANSITIONS->states[Num] a shift? (as opposed to gotos).  */

# define TRANSITION_IS_SHIFT(Transitions, Num) \
  (ISTOKEN (TRANSITION_SYMBOL (Transitions, Num)))

/* Is the TRANSITIONS->states[Num] a goto?. */

# define TRANSITION_IS_GOTO(Transitions, Num) \
  (!TRANSITION_IS_SHIFT (Transitions, Num))

/* Is the TRANSITIONS->states[Num] labelled by the error token?  */

# define TRANSITION_IS_ERROR(Transitions, Num) \
  (TRANSITION_SYMBOL (Transitions, Num) == errtoken->content->number)

/* When resolving a SR conflicts, if the reduction wins, the shift is
   disabled.  */

# define TRANSITION_DISABLE(Transitions, Num) \
  (Transitions->states[Num] = NULL)

# define TRANSITION_IS_DISABLED(Transitions, Num) \
  (Transitions->states[Num] == NULL)


/* Iterate over each transition over a token (shifts).  */
# define FOR_EACH_SHIFT(Transitions, Iter)                      \
  for (Iter = 0;                                                \
       Iter < Transitions->num                                  \
         && (TRANSITION_IS_DISABLED (Transitions, Iter)         \
             || TRANSITION_IS_SHIFT (Transitions, Iter));       \
       ++Iter)                                                  \
    if (!TRANSITION_IS_DISABLED (Transitions, Iter))


/* Return the state such SHIFTS contain a shift/goto to it on SYM.
   Abort if none found.  */
struct state *transitions_to (transitions *shifts, symbol_number sym);


/*-------.
| Errs.  |
`-------*/

typedef struct
{
  int num;
  symbol *symbols[1];
} errs;

errs *errs_new (int num, symbol **tokens);


/*-------------.
| Reductions.  |
`-------------*/

typedef struct
{
  int num;
  bitset *lookahead_tokens;
  /* Sorted ascendingly on rule number.  */
  rule *rules[1];
} reductions;



/*---------.
| states.  |
`---------*/

struct state_list;

struct state
{
  state_number number;
  symbol_number accessing_symbol;
  transitions *transitions;
  reductions *reductions;
  errs *errs;

  /* When an includer (such as ielr.c) needs to store states in a list, the
     includer can define struct state_list as the list node structure and can
     store in this member a reference to the node containing each state.  */
  struct state_list *state_list;

  /* Whether no lookahead sets on reduce actions are needed to decide
     what to do in state S.  */
  bool consistent;

  /* If some conflicts were solved thanks to precedence/associativity,
     a human readable description of the resolution.  */
  const char *solved_conflicts;
  const char *solved_conflicts_xml;

  /* Its items.  Must be last, since ITEMS can be arbitrarily large.  Sorted
     ascendingly on item index in RITEM, which is sorted on rule number.  */
  size_t nitems;
  item_number items[1];
};

extern state_number nstates;
extern state *final_state;

/* Create a new state with ACCESSING_SYMBOL for those items.  */
state *state_new (symbol_number accessing_symbol,
                  size_t core_size, item_number *core);
state *state_new_isocore (state const *s);

/* Set the transitions of STATE.  */
void state_transitions_set (state *s, int num, state **trans);

/* Set the reductions of STATE.  */
void state_reductions_set (state *s, int num, rule **reds);

int state_reduction_find (state *s, rule *r);

/* Set the errs of STATE.  */
void state_errs_set (state *s, int num, symbol **errors);

/* Print on OUT all the lookahead tokens such that this STATE wants to
   reduce R.  */
void state_rule_lookahead_tokens_print (state *s, rule *r, FILE *out);
void state_rule_lookahead_tokens_print_xml (state *s, rule *r,
                                            FILE *out, int level);

/* Create/destroy the states hash table.  */
void state_hash_new (void);
void state_hash_free (void);

/* Find the state associated to the CORE, and return it.  If it does
   not exist yet, return NULL.  */
state *state_hash_lookup (size_t core_size, item_number *core);

/* Insert STATE in the state hash table.  */
void state_hash_insert (state *s);

/* Remove unreachable states, renumber remaining states, update NSTATES, and
   write to OLD_TO_NEW a mapping of old state numbers to new state numbers such
   that the old value of NSTATES is written as the new state number for removed
   states.  The size of OLD_TO_NEW must be the old value of NSTATES.  */
void state_remove_unreachable_states (state_number old_to_new[]);

/* All the states, indexed by the state number.  */
extern state **states;

/* Free all the states.  */
void states_free (void);

#endif /* !STATE_H_ */
