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

#include "regexp.h"
#include <string.h>
#include <map>


namespace rejit {
namespace internal {

ostream& Regexp::OutputToIOStream(ostream& stream) const {  // NOLINT
  stream << "Regexp (";
  switch (type()) {
#define PRINT_REGEXP_TYPE(RegexpType) \
case k##RegexpType: stream << #RegexpType; break;
    LIST_REGEXP_TYPES(PRINT_REGEXP_TYPE)
#undef PRINT_REGEXP_TYPE
    default:
      UNREACHABLE();
  }
  stream << ") {" << entry_state_ << ", " << exit_state_ << "}";
  return stream;
}


MultipleChar::MultipleChar(char c) : MatchingRegexp(kMultipleChar) {
  chars_.push_back(c);
}


MultipleChar::MultipleChar(MultipleChar *mc)
  : MatchingRegexp(kMultipleChar), chars_(mc->chars_) {}


MultipleChar::MultipleChar(const string& str)
  : MatchingRegexp(kMultipleChar) {
  ASSERT(str.length() <= kMaxNodeLength);
  for (size_t i = 0; i < str.length(); i++) {
    chars_.push_back(str[i]);
  }
}


MultipleChar::MultipleChar(const char* first_char, unsigned count)
  : MatchingRegexp(kMultipleChar) {
  ASSERT(count <= kMaxNodeLength);
  for (const char* c = first_char; c < first_char + count; c++) {
    chars_.push_back(*c);
  }
}


Regexp* MultipleChar::DeepCopy() {
  MultipleChar* newre = new MultipleChar(&chars_[0], chars_.size());
  return newre;
}


ostream& MultipleChar::OutputToIOStream(ostream& stream) const {  // NOLINT
  stream << string("MultipleChar [");
  for (unsigned i = 0; i < chars_length(); i++) {
    stream << chars_[i];
  }
  stream << "] {" << entry_state_ << ", " << exit_state_ << "}";
  return stream;
}


ostream& Bracket::OutputToIOStream(ostream& stream) const {  // NOLINT
  stream << "Bracket ";
  if (flags() & non_matching)
    stream << "(non_matching) ";
    stream << "[ {" << entry_state_ << ", " << exit_state_ << "}\n";
  { IndentScope is(2);
    // TODO(rames): overkill!
    vector<char>::const_iterator cit;
    Indent(stream);
    for (cit = single_chars_.begin(); cit < single_chars_.end(); cit++) {
      stream << *cit;
    }
    stream << endl;
    vector<Bracket::CharRange>::const_iterator rit;
    for (rit = char_ranges_.begin(); rit < char_ranges_.end(); rit++) {
      Indent(stream) << (*rit).low << "-" << (*rit).high << endl;
    }
  }
  Indent(stream) << "]";
  return stream;
}


Regexp* Bracket::DeepCopy() {
  Bracket* bracket = new Bracket();
  bracket->single_chars_ = this->single_chars_;
  bracket->char_ranges_ = this->char_ranges_;

  return bracket;
}


void RegexpWithSubs::DeepCopySubRegexpsFrom(RegexpWithSubs* original) {
  vector<Regexp*>::const_iterator it;
  for (it = original->sub_regexps()->begin();
       it < original->sub_regexps()->end();
       it++) {
    this->sub_regexps()->push_back((*it)->DeepCopy());
  }
}


unsigned RegexpWithSubs::MatchLength() const {
  unsigned maximum = 0;
  vector<Regexp*>::const_iterator it;
  for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
    maximum = max(maximum, (*it)->MatchLength());
  }
  return maximum;
}


Regexp* Concatenation::DeepCopy() {
  Concatenation* newre = new Concatenation();
  newre->DeepCopySubRegexpsFrom(this);
  return newre;
}


ostream& Concatenation::OutputToIOStream(ostream& stream) const {  // NOLINT
  stream << "Concatenation [ {"
    << entry_state_ << ", " << exit_state_ << "}\n";
  { IndentScope is(2);
    vector<Regexp*>::const_iterator it;
    for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
      Indent(stream) << **it << endl;
    }
  }
  Indent(stream) << "]";
  return stream;
}


void Concatenation::SetEntryState(int entry_state) {
  entry_state_ = entry_state;
  sub_regexps_.at(0)->SetEntryState(entry_state);
}


void Concatenation::SetExitState(int exit_state) {
  exit_state_ = exit_state;
  sub_regexps_.back()->SetExitState(exit_state);
}


Regexp* Alternation::DeepCopy() {
  Alternation* newre = new Alternation();
  newre->DeepCopySubRegexpsFrom(this);
  return newre;
}


ostream& Alternation::OutputToIOStream(ostream& stream) const {  // NOLINT
  stream << "Alternation [ {"
                 << entry_state_ << ", " << exit_state_ << "}\n";
  {
    vector<Regexp*>::const_iterator it;
    IndentScope is(2);
    for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
      Indent(stream) << **it << endl;
    }
  }
  Indent(stream) << "]";
  return stream;
}


void Alternation::SetEntryState(int entry_state) {
  entry_state_ = entry_state;
  vector<Regexp*>::iterator it;
  for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
    (*it)->SetEntryState(entry_state);
  }
}


void Alternation::SetExitState(int exit_state) {
  exit_state_ = exit_state;
  vector<Regexp*>::iterator it;
  for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
    (*it)->SetExitState(exit_state);
  }
}


ostream& Repetition::OutputToIOStream(ostream& stream) const {  // NOLINT
  if (max_rep_ == kMaxUInt) {
    stream << "Repetition"
                   << "{" << min_rep_ << ", inf } [ {"
                   << entry_state_ << ", " << exit_state_ << "}\n";
  } else {
    stream << "Repetition"
                   << "{" << min_rep_ << "," << max_rep_ << "} [ {"
                   << entry_state_ << ", " << exit_state_ << "}\n";
  }
  {
    IndentScope is(2);
    Indent(stream) << *sub_regexp_ << endl;
  }
  Indent(stream) << "]";
  return stream;
}


Regexp* Repetition::DeepCopy() {
  return new Repetition(sub_regexp()->DeepCopy(), min_rep(), max_rep());
}


RegexpInfo::~RegexpInfo() {
  if (regexp_)              regexp_->~Regexp();
  if (vmem_match_full_)     delete vmem_match_full_;
  if (vmem_match_anywhere_) delete vmem_match_anywhere_;
  if (vmem_match_first_)    delete vmem_match_first_;
  if (vmem_match_all_)      delete vmem_match_all_;
  vector<Regexp*>::iterator it;
  for (it = extra_allocated_.begin(); it < extra_allocated_.end(); it++) {
    (*it)->~Regexp();
  }
}


void RegexpInfo::print_re_list() {
  cout << "Regexp list --------------------------------{{{" << endl;
  { IndentScope is(2);
    cout << "Control regexps list --------------------------------{{{" << endl;
    cout << "topologically sorted: ";
    cout << (re_control_list_topo_sorted_ ? "yes" : "no") << endl;
    for (ControlRegexp* re : re_control_list_) {
      cout << *re << endl;
    }
    cout << "}}}-------------------------- End of control regexp list" << endl;
    cout << "Matching regexps list --------------------------------{{{" << endl;
    for (MatchingRegexp* re : re_matching_list_) {
      cout << *re << endl;
    }
    cout << "}}}-------------------------- End of matching regexp list" << endl;
  }
  cout << "}}}------------------------- End of regexp list" << endl;
}


// Regexp utils ----------------------------------------------------------------

bool regexp_cmp_entry_state(Regexp* r1, Regexp* r2) {
  return r1->entry_state() < r2->entry_state();
}


bool regexp_cmp_exit_state(Regexp* r1, Regexp* r2) {
  return r1->exit_state() < r2->exit_state();
}


bool all_regexps_start_at(int entry_state, vector<Regexp*> *regexps) {
  vector<Regexp*>::const_iterator it;
      for (it = regexps->begin(); it < regexps->end(); it++) {
        if ((*it)->entry_state() != entry_state) {
          break;
        }
      }
      return it == regexps->end();
}


bool SortTopoligcal(vector<Regexp*> *regexps) {
  unsigned n_re = regexps->size();

  if (n_re <= 1) {
    return true;
  }

  // Map entry/exit states to regexps.
  multimap<int, Regexp*> entries;
  multimap<int, Regexp*> exits;
  for (Regexp* re : *regexps) {
    entries.insert(pair<int, Regexp*>(re->entry_state(), re->AsControlRegexp()));
    exits.insert(pair<int, Regexp*>(re->exit_state(), re->AsControlRegexp()));
  }

  vector<Regexp*> sorted_ctrl_regexps;

  vector<int> sorted_states;

  for (pair<int, Regexp*> p : entries) {
    if (exits.find(p.first) == exits.end()) {
      vector<int>::iterator it;
      for (it = sorted_states.begin(); it != sorted_states.end(); ++it) {
        if (*it == p.first) {
          break;
        }
      }
      if (it == sorted_states.end()) {
        sorted_states.push_back(p.first);
      }
    }
  }

  if (sorted_states.size() == n_re)
    return true;

  while (!sorted_states.empty()) {
    int current = sorted_states.back();

    sorted_states.pop_back();

    for (pair<int, Regexp*> p : range(entries.equal_range(current))) {
      sorted_ctrl_regexps.push_back(p.second);
      int exit = p.second->exit_state();

      pair<multimap<int, Regexp*>::iterator, multimap<int, Regexp*>::iterator> exit_range;
      multimap<int, Regexp*>::iterator it;
      exit_range = exits.equal_range(exit);
      for (it = exit_range.first; it != exit_range.second; ++it) {
        if (it->second == p.second) {
          exits.erase(it);
          break;
        }
      }
      if (exits.count(exit) == 0) {
        sorted_states.push_back(exit);
      }
    }
  }

  if (sorted_ctrl_regexps.size() == n_re) {
    regexps->assign(sorted_ctrl_regexps.begin(), sorted_ctrl_regexps.end());
    return true;
  } else {
    return false;
  }
}

} }  // namespace rejit::internal

