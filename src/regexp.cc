// Copyright (C) 2013 Alexandre Rames <alexandre@uop.re>
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

namespace rejit {
namespace internal {

ostream& Regexp::OutputToIOStream(ostream& stream) const {  // NOLINT
  Indent(stream) << "Regexp (";
  switch (type()) {
#define PRINT_REGEXP_TYPE(RegexpType) \
case k##RegexpType: stream << #RegexpType; break;
    LIST_REGEXP_TYPES(PRINT_REGEXP_TYPE)
#undef PRINT_REGEXP_TYPE
    default:
      UNREACHABLE();
  }
  stream << ") {" << entry_state_ << ", " << output_state_ << "}";
  return stream;
}


MultipleChar::MultipleChar(const char* first_char, unsigned count)
  : Regexp(kMultipleChar), chars_(first_char), chars_length_(count) {
  ASSERT(count <= kMaxNodeLength);
}


Regexp* MultipleChar::DeepCopy() {
  MultipleChar* newre = new MultipleChar(chars_, chars_length_);
  return newre;
}


ostream& MultipleChar::OutputToIOStream(ostream& stream) const {  // NOLINT
  Indent(stream) << string("MultipleChar [");
  for (unsigned i = 0; i < chars_length(); i++) {
    stream << chars_[i];
  }
  Indent(stream) << "] {" << entry_state_ << ", " << output_state_ << "}";
  return stream;
}


ostream& Bracket::OutputToIOStream(ostream& stream) const {  // NOLINT
  Indent(stream) << "Bracket [ {"
    << entry_state_ << ", " << output_state_ << "}\n";
  { IndentScope is;
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
  Indent(stream) << "Concatenation [ {"
    << entry_state_ << ", " << output_state_ << "}\n";
  { IndentScope is;
    vector<Regexp*>::const_iterator it;
    for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
      stream << **it << endl;
    }
  }
  Indent(stream) << "]";
  return stream;
}


void Concatenation::SetEntryState(int entry_state) {
  entry_state_ = entry_state;
  sub_regexps_.at(0)->SetEntryState(entry_state);
}


void Concatenation::SetOutputState(int output_state) {
  output_state_ = output_state;
  sub_regexps_.back()->SetOutputState(output_state);
}


Regexp* Alternation::DeepCopy() {
  Alternation* newre = new Alternation();
  newre->DeepCopySubRegexpsFrom(this);
  return newre;
}


ostream& Alternation::OutputToIOStream(ostream& stream) const {  // NOLINT
  Indent(stream) << "Alternation [ {"
                 << entry_state_ << ", " << output_state_ << "}\n";
  {
    vector<Regexp*>::const_iterator it;
    IndentScope is;
    for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
      stream << **it << endl;
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


void Alternation::SetOutputState(int output_state) {
  output_state_ = output_state;
  vector<Regexp*>::iterator it;
  for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
    (*it)->SetOutputState(output_state);
  }
}


ostream& Repetition::OutputToIOStream(ostream& stream) const {  // NOLINT
  if (max_rep_ == kMaxUInt) {
    Indent(stream) << "Repetition"
                   << "{" << min_rep_ << ", inf } [ {"
                   << entry_state_ << ", " << output_state_ << "}\n";
  } else {
    Indent(stream) << "Repetition"
                   << "{" << min_rep_ << "," << max_rep_ << "} [ {"
                   << entry_state_ << ", " << output_state_ << "}\n";
  }
  {
    IndentScope is;
    stream << *sub_regexp_ << endl;
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


// Regexp utils ----------------------------------------------------------------

// A positive return value means that r1 is better than r2 for fast forwarding.
int ff_phy_cmp(Regexp* r1, Regexp* r2) {
  ASSERT(r1->IsPhysical() && r2->IsPhysical());
  if (r1->IsMultipleChar() && r2->IsMultipleChar()) {
    return r1->AsMultipleChar()->chars_length() >
      r2->AsMultipleChar()->chars_length();
  }

  return r2->type() - r1->type();
}


} }  // namespace rejit::internal

