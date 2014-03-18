// Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>
// rejit is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "codegen.h"
#include "macro-assembler.h"
#include <fstream>

#include "suffix_trees.h"


namespace rejit {
namespace internal {


static void print_match(Match m) {
    const char* c = m.begin;
    printf("match (start %p - end %p) (text matched: ", m.begin, m.end);
    while (c < m.end) {
      printf("%c", *c++);
    }
    printf(")");
}


static void MatchAllAppend(vector<Match>* matches, Match new_match, bool filter) {
  // The matches in the vector must be disjoint and in increasing order.
  // This also assumes that no matches finishing after the new match have been
  // registered already.

  if (FLAG_trace_match_all) {
    printf("Found: ");
    print_match(new_match);
    printf("\n");
  }

  if (filter && matches->size()) {
    vector<Match>::iterator it;
    for (it = matches->end() - 1;
         it >= matches->begin() && (*it).begin >= new_match.begin;
         --it) {}
    // Point to where the next match should be inserted.
    ++it;
    if (FLAG_trace_match_all && it != matches->end()) {
      printf("Deleting %ld previously registered matches:",
             distance(it, matches->end()));
      for (; it < matches->end(); ++it) {
        print_match(*it);
        printf("\n");
      }
    }
    matches->erase(it, matches->end());
  }

  // The behaviour for matches of length 0 is a bit special. For now this is
  // made to behavie like in vim. An end-of-line match (regexp '$') is added
  // only if there is not already a match finishing at that position.
  // TODO: This may need to be modified after checking the spec.
  if (new_match.begin == new_match.end &&
      !matches->empty() &&
      new_match.begin == matches->back().end) {
    return;
  }

  matches->push_back(new_match);
}

void MatchAllAppendRaw(vector<Match>* matches, Match new_match) {
  // The generated code has ensured that the match registred is valid.
  MatchAllAppend(matches, new_match, false);
}


void MatchAllAppendFilter(vector<Match>* matches, Match new_match) {
  MatchAllAppend(matches, new_match, true);
}


// RegexpIndexer ---------------------------------------------------------------

void RegexpIndexer::Index(Regexp* root) {
  IndexSub(root);
  rinfo_->set_entry_state(0);
  rinfo_->set_exit_state(this->entry_state());
}


void RegexpIndexer::IndexSub(Regexp* root, int entry, int exit) {
  Visit(root);
  root->SetEntryState(entry);
  if (exit != -1) {
    root->SetExitState(exit);
  }
  rinfo_->set_last_state(last_state());
}


// TODO(rames): If the definitions stay so small and don't diverge they could be
// changed to inline functions.
void RegexpIndexer::VisitRegexp(Regexp* re) {
  re->SetEntryState(entry_state_);
  re->SetExitState(++last_state_);
  entry_state_ = re->exit_state();
}


void RegexpIndexer::VisitAlternation(Alternation* alternation) {
  int original_entry = entry_state_;
  vector<Regexp*>::iterator it;
  for (it = alternation->sub_regexps()->begin();
       it < alternation->sub_regexps()->end();
       it++) {
    Visit(*it);
    last_state_--;
  }
  last_state_++;
  alternation->SetEntryState(original_entry);
  alternation->SetExitState(last_state_);
  entry_state_ = alternation->exit_state();
}


void RegexpIndexer::VisitConcatenation(Concatenation* concatenation) {
  int original_entry = entry_state_;
  vector<Regexp*>::iterator it;
  for (it = concatenation->sub_regexps()->begin();
       it < concatenation->sub_regexps()->end();
       it++) {
    Visit(*it);
  }
  concatenation->SetEntryState(original_entry);
  concatenation->SetExitState(last_state_);
  entry_state_ = concatenation->exit_state();
}


void RegexpIndexer::VisitRepetition(Repetition* re) {
  // The actual work will be done by the RegexpLister.
  VisitRegexp(re);
}


// RegexpIndexer ---------------------------------------------------------------

void RegexpLister::VisitAlternation(Alternation* regexp) {
  vector<Regexp*>::iterator it;
  for (it = regexp->sub_regexps()->begin();
       it < regexp->sub_regexps()->end();
       it++ ) {
    Visit(*it);
  }
}


void RegexpLister::VisitConcatenation(Concatenation* regexp) {
  vector<Regexp*>::iterator it;
  for (it = regexp->sub_regexps()->begin();
       it < regexp->sub_regexps()->end();
       it++) {
    Visit(*it);
  }
}


void RegexpLister::VisitRepetition(Repetition* repetition) {
  // TODO: HIGH_PRIORITY Better handling of repetitions.
  // Base may have been referenced by other mechanisms. It must be indexed.
  Regexp* base = repetition->sub_regexp();
  unsigned min_rep = repetition->min_rep();
  unsigned max_rep = repetition->max_rep();
  bool is_limited = repetition->IsLimited();

  if (min_rep == 0 && max_rep == 0) {
    Epsilon* eps_bypass =
        new Epsilon(repetition->entry_state(), repetition->exit_state());
    ListNew(eps_bypass);
    if (FLAG_trace_repetitions) {
      cout << "Repetion ----------" << endl;
      cout << *eps_bypass << endl;
      cout << "---------- End of repetition" << endl;
    }
    return;
  }

  vector<Regexp*>* tracing = NULL;
  if (FLAG_trace_repetitions) {
    tracing = new vector<Regexp*>;
  }

  // For an unlimited repetition we aim to produce:
  //           /                    __eps__
  // epsilons |                    |       |
  //           \                   v       |
  // inside:         O--x-->O--x-->O---x-->O--eps-->O
  //
  // This should be better when matching forward, but worse when matching
  // backward.
  //
  // For the kleene operator we aim to produce:
  //                  ___________eps___________
  //           /     |         __eps__         |
  // epsilons |      |        |       |        |
  //           \     |        v       |        v
  // inside:         O--eps-->O---x-->O--eps-->O
  //
  // For x{0,3} we aim to produce:
  //           /      _______=eps==========
  // epsilons |      |      |       |      |
  //           \     |      |       |      v
  // inside:         O--x-->O---x-->O--x-->O

  // Prepare the 'inside' regexps.
  // Note that the base regexp may have been selected for foast-forwarding, so
  // *must* be in the inside regexp, and not in the optional part of the regexps
  // generated.
  Regexp* inside = NULL;

  bool needs_concatenation = min_rep > 1 || (max_rep > 1 && is_limited);
  if (!needs_concatenation) {
    inside = base;
  } else {
    unsigned n_rep = is_limited ? max_rep : min_rep;
    Concatenation *concat = new Concatenation();
    concat->Append(base);
    for (unsigned i = 1; i < n_rep; i++) {
      Regexp *repeated = base->DeepCopy();
      rinfo()->extra_allocated()->push_back(repeated);
      concat->Append(repeated);
    }
    inside = concat;
  }
  if (tracing) tracing->push_back(inside);

  int inside_entry_state = repetition->entry_state();
  int inside_exit_state = repetition->exit_state();
  if (!is_limited) {
    // We need an extra state at the end.
    inside_exit_state = -1;
    if (min_rep <= 1) {
      // We need an extra state at the beginning.
      rinfo()->set_last_state(rinfo()->last_state() + 1);
      inside_entry_state = rinfo()->last_state();
    }
  }
  RegexpIndexer indexer(rinfo(),
                        inside_entry_state,
                        rinfo()->last_state());
  indexer.IndexSub(inside, inside_entry_state, inside_exit_state);

  Visit(inside);


  // Generate the appropriate epsilon transitions.

  // Bypass epsilon.
  if (min_rep == 0) {
    Epsilon* eps_bypass =
        new Epsilon(repetition->entry_state(), repetition->exit_state());
    ListNew(eps_bypass);
    if (tracing) tracing->push_back(eps_bypass);
  }

  if (is_limited && max_rep > 1) {
    if (max_rep > 1) {
      Concatenation* concat = inside->AsConcatenation();
      vector<Regexp*>::const_iterator it;
      int min = max(1u, min_rep);
      for (it = concat->sub_regexps()->begin() + min - 1;
           it < concat->sub_regexps()->end() - 1;
           it++) {
        Epsilon *eps_exit = new Epsilon((*it)->exit_state(),
                                        repetition->exit_state());
        ListNew(eps_exit);
        if (tracing) tracing->push_back(eps_exit);
      }
    } else {
      // Nothing to do.
    }

  } else {
    // Entry epsilon.
    if (min_rep <= 1) {
      Epsilon *eps_entry = new Epsilon(repetition->entry_state(), inside->entry_state());
      ListNew(eps_entry);
      if (tracing) tracing->push_back(eps_entry);
    }

    // Exit epsilon.
    Epsilon *eps_exit = new Epsilon(inside->exit_state(), repetition->exit_state());
    ListNew(eps_exit);
    if (tracing) tracing->push_back(eps_exit);

    // Repeat epsilon.
    Regexp *last;
    if (!needs_concatenation) {
      last = inside;
    } else {
      last = inside->AsConcatenation()->sub_regexps()->back();
    }
    Epsilon *eps_rep = new Epsilon(last->exit_state(), last->entry_state());
    ListNew(eps_rep);
    if (tracing) tracing->push_back(eps_rep);
  }


  if (FLAG_trace_repetitions) {
    cout << "Repetion ----------" << endl;
    for (Regexp *re : *tracing) {
      cout << *re << endl;
    }
    cout << "---------- End of repetition" << endl;
    delete tracing;
  }
}


void FF_finder::FindFFElements() {
  if (Visit(rinfo_->regexp())) {
    if (FLAG_use_ff_reduce) {
      size_t start = 0, end = ff_list_->size();
      ff_alternation_reduce(&start, &end);
    }
  } else {
    rinfo_->ff_list()->clear();
  }
}


bool FF_finder::VisitAlternation(Alternation* alt) {
  bool res = true;
  vector<Regexp*>::iterator it;
  for (it = alt->sub_regexps()->begin();
       it < alt->sub_regexps()->end();
       it++) {
    res &= Visit(*it);
  }
  return res;
}


// TODO: Clean this.
bool FF_finder::VisitConcatenation(Concatenation* concat) {
  bool res = false;
  bool cur_res;
  vector<Regexp*>::iterator it;
  size_t cur_start = ff_list_->size();
  size_t cur_end = ff_list_->size();

  for (it = concat->sub_regexps()->begin();
       it < concat->sub_regexps()->end();
       it++) {
    cur_res = Visit(*it);
    if (!cur_res) {
      ff_list_->erase(ff_list_->begin() + cur_end, ff_list_->end());
    }
    res |= cur_res;
    if (cur_start == cur_end) {
      // No sub-regexp has been visited successfully yet.
      cur_end = ff_list_->size();
    }

    // Chose between the two blocks of regexps.
    if (ff_reduce_cmp(&cur_start, &cur_end) >= 0) {
      ff_list_->erase(ff_list_->begin() + cur_end, ff_list_->end());
    } else {
      ff_list_->erase(ff_list_->begin() + cur_start,
                      ff_list_->begin() + cur_end);
    }
    cur_end = ff_list_->size();
  }

  return res;
}


bool FF_finder::VisitRepetition(Repetition* rep) {
  if (rep->min_rep() > 0) {
    Visit(rep->sub_regexp());
  } else {
    return false;
  }
  return true;
}

void FF_finder::ff_alternation_reduce(size_t *start, size_t *end) {
  vector<Regexp*>::iterator it;
  if (*end - *start <= 1) {
    return;
  }

  // Build a suffix tree for the mcs involved.
  vector<Regexp*> mcs(ff_list_->size());
  it = copy_if(ff_list_->begin() + *start, ff_list_->begin() + *end,
               mcs.begin(),
               [](Regexp* re) { return re->IsMultipleChar(); });
  mcs.resize(distance(mcs.begin(), it));
  if (mcs.size() < 2) {
    return;
  }
  SuffixTreeBuilder st_builder;
  for (Regexp *re : mcs) {
    st_builder.append_mc(re->AsMultipleChar());
  }
  SuffixTree *root = st_builder.root();

  // Traverse the tree to compute the best substring.
  // TODO: We currently retrieve one substring. Ideally we should get the best
  // partition of substrings.
  const SuffixTree *best = lowest_common_ancestor(root, mcs.size());
  if (!best || !best->str()) {
    return;
  }
  string longest_substring = string(best->str(),
                                    best->str_end() - best->active_length(),
                                    best->active_length());
  // Now see if this string would be more efficient than the alternation as a
  // fast-forward element.
  int mcs_score = 0;
  for (Regexp *re : mcs) {
    mcs_score += re->AsMultipleChar()->ff_score();
  }
  if (mcs_score < MultipleChar::ff_score(longest_substring.length())) {
    return;
  }

  // Create a new mc for the substring.
  MultipleChar *substring_mc = new MultipleChar(longest_substring);
  int last_state = rinfo_->last_state();
  int substring_entry_state = last_state + 1;
  int substring_exit_state = last_state + 2;
  substring_mc->SetEntryState(substring_entry_state);
  substring_mc->SetExitState(substring_exit_state);
  rinfo_->set_last_state(last_state + 2);
  rinfo_->extra_allocated()->push_back(substring_mc);
  // Note that this new mc is *not* added to the gen_list. This would create
  // incorrect transitions.

  // Remove the elements replaced by the new mc.
  vector<Regexp*>::iterator it_mcs;
  for (it_mcs = mcs.begin(); it_mcs < mcs.end(); ++it_mcs) {
    for (it = ff_list_->begin() + *start; it < ff_list_->begin() + *end; ++it) {
      if (*it == *it_mcs) {
        ff_list_->erase(it);
        --(*end);
        break;
      }
    }
  }
  ff_list_->insert(ff_list_->begin() + *start, substring_mc);
  ++(*end);

  if (FLAG_print_ff_reduce) {
    cout << "Fast-forward elements reduction ------------{{{" << endl;
    cout << "The following ff-elements:" << endl;
    { IndentScope is(4);
      for (Regexp *re : mcs) {
        Indent(cout) << *re << endl;
      }
    }
    cout << "are being replaced by the ff-element:" << endl;
    { IndentScope is(4);
      Indent(cout) << *substring_mc << endl;
    }
    cout << "and the following linking regexps:" << endl;
  }

  // Insert the regexps linking the new ff-element to their respective
  // associated regexps.
  for (it_mcs = mcs.begin(); it_mcs < mcs.end(); ++it_mcs) {
    MultipleChar *mc = (*it_mcs)->AsMultipleChar();

    string mc_string = string(mc->chars(), mc->chars_length());
    size_t substring_offset = mc_string.find(longest_substring);
    ASSERT(substring_offset != string::npos);
    Regexp *re_in, *re_out;

    if (substring_offset != 0) {
      MultipleChar *linking_mc_in = new MultipleChar(mc->chars(),
                                                     substring_offset);
      linking_mc_in->SetEntryState(mc->entry_state());
      linking_mc_in->SetExitState(substring_entry_state);
      rinfo_->extra_allocated()->push_back(linking_mc_in);
      rinfo_->re_matching_list()->push_back(linking_mc_in);
      re_in = linking_mc_in;
    } else {
      Epsilon *epsilon = new Epsilon(mc->entry_state(), substring_entry_state);
      rinfo_->extra_allocated()->push_back(epsilon);
      rinfo_->re_control_list()->push_back(epsilon);
      re_in = epsilon;
    }

    if (substring_offset + longest_substring.length() != mc_string.length()) {
      MultipleChar *linking_mc_out =
        new MultipleChar(mc->chars() + substring_offset + longest_substring.length());
      linking_mc_out->SetEntryState(substring_exit_state);
      linking_mc_out->SetExitState(mc->exit_state());
      rinfo_->extra_allocated()->push_back(linking_mc_out);
      rinfo_->re_matching_list()->push_back(linking_mc_out);
      re_out = linking_mc_out;
    } else {
      Epsilon *epsilon = new Epsilon(substring_exit_state, mc->exit_state());
      rinfo_->extra_allocated()->push_back(epsilon);
      rinfo_->re_control_list()->push_back(epsilon);
      re_out = epsilon;
    }

    if (FLAG_print_ff_reduce) {
      { IndentScope is(2);
        Indent(cout) << "(for " << *mc << " :)" << endl;
      }
      { IndentScope is(4);
        Indent(cout) << *re_in << endl;
        Indent(cout) << *re_out << endl;
      }
    }
  }
  if (FLAG_print_ff_reduce) {
    cout << "}}}----- End of fast-forward elements reduction" << endl;
  }
  rinfo_->set_ff_reduced(true);
}

int FF_finder::ff_reduce_cmp(size_t *i1, size_t *i2) {
  size_t list_size = ff_list_->size();
  size_t s1 = *i2 - *i1;
  size_t s2 = list_size - *i2;
  int score_1 = 0, score_2 = 0;

  // We need the lowest score, but we need some regexps to look for !
  if (s1 == 0) return -1;
  if (s2 == 0) return  1;

  if (FLAG_use_ff_reduce) {
    ff_alternation_reduce(i1, i2);
    list_size = ff_list_->size();
    ff_alternation_reduce(i2, &list_size);
  }

  for (size_t i = *i1; i < *i2; i++) {
    score_1 += ff_list_->at(i)->ff_score();
  }
  for (size_t i = *i2; i < list_size; i++) {
    score_2 += ff_list_->at(i)->ff_score();
  }

  return score_2 - score_1;
}


// Codegen ---------------------------------------------------------------------

static void dump_code(RegexpInfo *rinfo, VirtualMemory *vmem) {
  static unsigned index = 1;
  char *dump_name;
  ofstream dump_file;
  unsigned n_digits = 0;

  for (unsigned tmp = index; tmp != 0; tmp = tmp / 10) {
    ++n_digits;
  }
  dump_name = (char*)malloc(strlen("dump.info.") + n_digits + 1);
  ASSERT(dump_name!= NULL);

  sprintf(dump_name, "dump.%d", n_digits);
  dump_file.open(dump_name, ofstream::binary);
  dump_file.write((char*)vmem->address(), vmem->size());
  dump_file.close();

  sprintf(dump_name, "dump.info.%d", n_digits);
  dump_file.open(dump_name);
  dump_file << "Regexp: " << rinfo->regexp() << endl;
  dump_file << "Base address: 0x" << hex << (uint64_t)vmem->address() << endl;
  dump_file.close();

  free(dump_name);

  ++index;
}


VirtualMemory* Codegen::Compile(RegexpInfo* rinfo, MatchType match_type) {
  rinfo_ = rinfo;
  match_type_ = match_type;

  Regexp* root = rinfo_->regexp();
  RegexpIndexer indexer(rinfo_);
  indexer.Index(root);
  if (FLAG_print_re_tree) {
    cout << "Regexp tree --------------------------------{{{" << endl;
    Indent(cout) << *root << endl;
    cout << "}}}------------------------- End of regexp tree" << endl;
  }

  RegexpLister lister(rinfo_);
  lister.Visit(root);

  FF_finder fff(rinfo);
  fff.FindFFElements();

  int n_states = rinfo->last_state() + 1;

  // Align size with cache line size?
  state_ring_time_size_ = kPointerSize * n_states;

  state_ring_times_ = 1 + min(rinfo_->regexp_max_length(), kMaxNodeLength);

  state_ring_size_ = state_ring_time_size_ * state_ring_times_;

  // TODO(rames): Investigate if rounding the state ring size to a power of 2
  // could speedup state related operations.

  // Bit <n> set in the time summary indicates that there is at least one state
  // set for time <n>.
  // A bit clear indicates that no states are set.
  time_summary_size_ = kPointerSize * ((state_ring_times_ / kBitsPerPointer) +
    ((state_ring_times_ % kBitsPerPointer) != 0));
  
  ring_base_ = Operand(rbp, StateRingBaseOffsetFromFrame());

  if (FLAG_print_state_ring_info) {
    cout << "State ring info ----------------------------{{{" << endl;
    cout << "n_states : " << n_states << endl;
    cout << "state_ring_time_size_ : " << state_ring_time_size_ << endl;
    cout << "state_ring_times_ : " << state_ring_times_ << endl;
    cout << "state_ring_size_ : " << state_ring_size_ << endl;
    cout << "time_summary_size_ : " << time_summary_size_ << endl;
    cout << "}}}--------------------- End of state ring info" << endl;
  }

  if (FLAG_print_re_list) {
    rinfo->print_re_list();
  }

  vector<Regexp*>* re_control_list =
      reinterpret_cast<vector<Regexp*>*>(rinfo->re_control_list());
  rinfo->set_re_control_list_top_sorted(SortTopoligcal(re_control_list));

  Generate();

  rinfo_ = NULL;
  VirtualMemory* vmem = masm_->GetCode();
  if (FLAG_dump_code) {
    dump_code(rinfo, vmem);
  }
  return vmem;
}


} }  // namespace rejit::internal

