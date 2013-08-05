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

#ifndef REJIT_REGEXP_H_
#define REJIT_REGEXP_H_

#include "globals.h"
#include "platform.h"
#include "utils.h"

namespace rejit {
namespace internal {

// Matching regular expression directly match characters.
// Order matters. See aliases below.
#define LIST_MATCHING_REGEXP_TYPES(M)                                          \
  M(MultipleChar)                                                              \
  M(Period)                                                                    \
  M(Bracket)
// Control regexps check for conditions and may have side effects, but don't
// match characters from the text.
#define LIST_CONTROL_REGEXP_TYPES(M)                                           \
  M(StartOfLine)                                                               \
  M(EndOfLine)                                                                 \
  M(Epsilon)

// The codegen generates code for physical regexps.
#define LIST_PHYSICAL_REGEXP_TYPES(M)                                          \
  LIST_MATCHING_REGEXP_TYPES(M)                                                \
  LIST_CONTROL_REGEXP_TYPES(M)

#define LIST_FLOW_REGEXP_TYPES(M)                                              \
  M(Concatenation)                                                             \
  M(Repetition)                                                                \
  M(Alternation)

// Real regular expression can appear in a regular expression tree, after
// parsing has finished and succeded.
// There is a sub class of Regexp for each of these.
#define LIST_REAL_REGEXP_TYPES(M)                                              \
  LIST_PHYSICAL_REGEXP_TYPES(M)                                                \
  LIST_FLOW_REGEXP_TYPES(M)

// Virtual regular expression types. They are only used at parsing time.
// No matching classes exist for these.
#define LIST_VIRTUAL_REGEXP_TYPES(M)                                           \
  M(LeftParenthesis)                                                           \
  M(AlternateBar)

// The enumeration order of the regexp types matters.
// Regexp types listed first are 'faster' to match. The fast forward mechanisms
// rely on this order.
// Left parenthesis and alternate bar must be defined last, as
// Regexp::IsMarker depends on it.
#define LIST_REGEXP_TYPES(M)                                                   \
  LIST_REAL_REGEXP_TYPES(M)                                                    \
  LIST_VIRTUAL_REGEXP_TYPES(M)


#define ENUM_REGEXP_TYPES(RegexpType) k##RegexpType,
  enum RegexpType {
    LIST_REGEXP_TYPES(ENUM_REGEXP_TYPES)
    // Aliases.
    kLastPhysicalRegexp = kEpsilon,
    kFirstControlRegexp = kStartOfLine,
    kLastControlRegexp = kEpsilon,
    kFirstMarker = kLeftParenthesis
  };
#undef ENUM_REGEXP_TYPES


// Forward declaration of real regexp classes.
class Regexp;
#define FORWARD_DECLARE(RegexpType) class RegexpType;
LIST_REAL_REGEXP_TYPES(FORWARD_DECLARE)
#undef FORWARD_DECLARE


// Limit the maximum length of a regexp to limit the maximum size of the state
// ring.
// TODO: This should probably be computed to match some architectural
// limit related to the caches.
// TODO: This could be modifiable via a flag.
static const unsigned kMaxNodeLength = 64;


// Regexps ---------------------------------------------------------------------

// The base class for Regexps.
// The parser builds a tree of Regexps, that is then passed to a code generator
// to generate some code matching the respresented regular expression.
class Regexp {
 public:
  explicit Regexp(RegexpType type) :
    type_(type), entry_state_(-1), output_state_(-1) {}
  virtual ~Regexp() {}
  // TODO: This is necessary because of the way we handle repetitions. See the
  // Repetition class.
  inline virtual Regexp* DeepCopy() {
    Regexp* newre = new Regexp(type_);
    return newre;
  }

#define DECLARE_IS_REGEXP_HELPERS(RegexpType)                                  \
  bool Is##RegexpType() { return type_ == k##RegexpType; }
  LIST_REGEXP_TYPES(DECLARE_IS_REGEXP_HELPERS)
#undef DECLARE_IS_REGEXP_HELPERS

#define DECLARE_CAST(RegexpType)                                               \
  inline RegexpType* As##RegexpType() {                                        \
    ASSERT(Is##RegexpType());                                                  \
    return reinterpret_cast<RegexpType*>(this);                                \
  }
  LIST_REAL_REGEXP_TYPES(DECLARE_CAST)
#undef DECLARE_CAST

  inline bool IsControlRegexp() {
    return kFirstControlRegexp <= type() && type() <= kLastControlRegexp;
  }

  inline bool IsPhysical() {
    return type() <= kLastPhysicalRegexp;
  }

  // Left parenthesis and vertical bar are markers for the parser.
  inline bool IsMarker() const { return type_ >= kFirstMarker; }

  // The maximum number of characters matched by this regexp.
  // This is used to determine how many times must be allocated for the state
  // ring.
  // TODO: Introduce min, and known match lengths?
  virtual unsigned MatchLength() const { return 0; }
  // The score used to decide what regular expressions are used for fast
  // forward. Scores were decided considering relative performance of different
  // regexps.
  // A lower score indicates a regexp easier to match for the ff mechanisms.
  static const int ff_base_score = 116;
  virtual int ff_score() const { UNREACHABLE(); return 0; }

  inline virtual void SetEntryState(int entry) { entry_state_ = entry; }
  inline virtual void SetOutputState(int output) { output_state_ = output; }

  // Debug helpers.
  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  // Accessors.
  inline RegexpType type() const { return type_; }
  inline int entry_state() const { return entry_state_; }
  inline int output_state() const { return output_state_; }

 protected:
  const RegexpType type_;
  int entry_state_;
  int output_state_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Regexp);
};


// Overload the '<<' operator to allow convenient printing to an iostream.
inline ostream& operator<<(ostream& stream, const Regexp& regexp) {
  regexp.OutputToIOStream(stream);
  return stream;
}


// TODO: Implementation of MC assumes that the regexp stays available for
// the lifetime of the generated functions.
class MultipleChar : public Regexp {
 public:
  MultipleChar() : Regexp(kMultipleChar) {}
  MultipleChar(char c);
  MultipleChar(const char* chars, unsigned count);
  virtual Regexp* DeepCopy();

  inline bool IsFull() {
    ASSERT(chars_length() <= kMaxNodeLength);
    return chars_length() == kMaxNodeLength;
  }
  void PushChar(char c) {
    ASSERT(!IsFull());
    chars_.push_back(c);
  }

  virtual unsigned MatchLength() const { return chars_length(); }
  virtual int ff_score() const {
    return chars_length() > 1 ? ff_base_score - max(16u, chars_length())
                              : 7 * ff_base_score + ff_base_score / 2;
  }

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  const char* chars() const { return &chars_[0]; }
  unsigned chars_length() const { return chars_.size(); }

  int64_t first_chars() const {
    return *reinterpret_cast<const int64_t*>(chars()) &
      FirstCharsMask(chars_length());
  }

  int64_t imm_chars() const {
    return *reinterpret_cast<const int64_t*>(chars()) &
      FirstCharsMask(chars_length());
  }

 protected:
  vector<char> chars_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultipleChar);
};


// TODO: The code generated for simple 'period' regexps could be grouped into
// one 'check for period' and state transition operations.
class Period : public Regexp {
 public:
  Period() : Regexp(kPeriod) {}
  virtual Regexp* DeepCopy() { return new Period(); }
  virtual unsigned MatchLength() const { return 1; }
  virtual int ff_score() const { return 20 * ff_base_score; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Period);
};


class Bracket : public Regexp {
 public:
  Bracket() : Regexp(kBracket), flags_(0) {}

  struct CharRange {
    char low;
    char high;
  };

  enum BracketFlag {
    non_matching = 1 << 0
  };

  virtual Regexp* DeepCopy();
  virtual unsigned MatchLength() const { return 1; }
  virtual int ff_score() const { return 15 * ff_base_score; }

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  inline void AddSingleChar(char c) { single_chars_.push_back(c); }
  inline void AddCharRange(CharRange range) { char_ranges_.push_back(range); }

  // Accessors.
  uint32_t flags() const { return flags_; }
  void set_flags(uint32_t flags) { flags_ = flags; }
  void set_flag(BracketFlag flag) { flags_ |= flag; }
  void clear_flag(BracketFlag flag) { flags_ &= ~flag; }
  vector<char>* single_chars() { return &single_chars_; }
  vector<CharRange>* char_ranges() { return &char_ranges_; }

 private:
  uint32_t flags_;
  vector<char> single_chars_;
  vector<CharRange> char_ranges_;

  DISALLOW_COPY_AND_ASSIGN(Bracket);
};


// Control regexp don't match characters from the input. They check for
// conditions or/and have side effects.
class ControlRegexp : public Regexp {
 protected:
  explicit ControlRegexp(RegexpType type) : Regexp(type) {}

  // Control regexp never match physical characters.
  virtual unsigned MatchLength() const { return 0; }
};


class StartOfLine : public ControlRegexp {
 public:
  StartOfLine() : ControlRegexp(kStartOfLine) {}
  inline virtual Regexp* DeepCopy() { return new StartOfLine(); }
  virtual int ff_score() const { return 2 * ff_base_score + ff_base_score / 2; }

 private:
  DISALLOW_COPY_AND_ASSIGN(StartOfLine);
};


class EndOfLine : public ControlRegexp {
 public:
  EndOfLine() : ControlRegexp(kEndOfLine) {}
  inline Regexp* DeepCopy() { return new EndOfLine(); }
  virtual int ff_score() const { return 2 * ff_base_score + ff_base_score / 2; }

 private:
  DISALLOW_COPY_AND_ASSIGN(EndOfLine);
};


class Epsilon : public ControlRegexp {
 public:
  // Epsilon transitions are created when handling repetitions, and cannot
  // appear in the regexp tree. So they are never indexed, but instead always
  // created with known entry and output states.
  explicit Epsilon(int entry, int output)
    : ControlRegexp(kEpsilon) {
      entry_state_ = entry;
      output_state_ = output;
    }

 private:
  DISALLOW_COPY_AND_ASSIGN(Epsilon);
};


class RegexpWithSubs : public Regexp {
 public:
  explicit RegexpWithSubs(RegexpType regexp_type) : Regexp(regexp_type) {}
  virtual ~RegexpWithSubs() {
    vector<Regexp*>::iterator it;
    for (it = sub_regexps_.begin(); it < sub_regexps_.end(); it++) {
      (*it)->~Regexp();
    }
  }

  void DeepCopySubRegexpsFrom(RegexpWithSubs* regexp);

  virtual unsigned MatchLength() const;

  // Accessors.
  vector<Regexp*>* sub_regexps() { return &sub_regexps_; }
 protected:
  vector<Regexp*> sub_regexps_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegexpWithSubs);
};


class Concatenation : public RegexpWithSubs {
 public:
  Concatenation() : RegexpWithSubs(kConcatenation) {}
  virtual Regexp* DeepCopy();

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  inline void Append(Regexp* regexp) {
    sub_regexps()->push_back(regexp);
  }

  virtual void SetEntryState(int entry_state);
  virtual void SetOutputState(int output_state);

 private:
  DISALLOW_COPY_AND_ASSIGN(Concatenation);
};


class Alternation : public RegexpWithSubs {
 public:
  Alternation() : RegexpWithSubs(kAlternation) {}
  virtual Regexp* DeepCopy();

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  virtual void SetEntryState(int entry_state);
  virtual void SetOutputState(int output_state);

 private:
  DISALLOW_COPY_AND_ASSIGN(Alternation);
};


class RegexpWithOneSub : public Regexp {
 public:
  explicit RegexpWithOneSub(RegexpType regexp_type, Regexp* sub_regexp)
    : Regexp(regexp_type), sub_regexp_(sub_regexp) {}
  virtual ~RegexpWithOneSub() { sub_regexp_->~Regexp(); }
  virtual Regexp* DeepCopy() = 0;

  // Accessors.
  Regexp* sub_regexp() { return sub_regexp_; }

 protected:
  Regexp* sub_regexp_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegexpWithOneSub);
};


// TODO: The current handling of repetitions is really poor. See code
// generation. It should refactored to avoid copying the sub-regexps and
// generating multiple times the associated matching code.
class Repetition : public RegexpWithOneSub {
 public:
  Repetition(Regexp* sub_regexp, uint32_t min_rep, uint32_t max_rep)
    : RegexpWithOneSub(kRepetition, sub_regexp),
      min_rep_(min_rep),
      max_rep_(max_rep) {}
  virtual Regexp* DeepCopy();

  virtual unsigned MatchLength() const { return sub_regexp_->MatchLength(); }

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  bool IsLimited() const { return max_rep() != kMaxUInt; }

  // Accessors.
  uint32_t min_rep() const { return min_rep_; }
  uint32_t max_rep() const { return max_rep_; }

 private:
  // Minimum and maximum number of repetition allowed.
  uint32_t min_rep_;
  // kMaxUInt marks an infinite number of repetitions.
  uint32_t max_rep_;

  DISALLOW_COPY_AND_ASSIGN(Repetition);
};


// Regexp visitors -------------------------------------------------------------

template <class ret_type> class RealRegexpVisitor {
 public:
  RealRegexpVisitor() {}
  virtual ~RealRegexpVisitor() {}

  ret_type Visit(Regexp* regexp) {
    switch (regexp->type()) {
#define TYPE_CASE(RegexpType)                                                  \
      case k##RegexpType:                                                      \
        return Visit##RegexpType(reinterpret_cast<RegexpType*>(regexp));       \
        break;
      LIST_REAL_REGEXP_TYPES(TYPE_CASE)
#undef TYPE_CASE
      default:
        UNREACHABLE();
        return static_cast<ret_type>(0);
    }
  }

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual ret_type Visit##RegexpType(RegexpType* r) = 0;
  LIST_REAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

 private:
  DISALLOW_COPY_AND_ASSIGN(RealRegexpVisitor);
};

template <class ret_type> class PhysicalRegexpVisitor {
 public:
  PhysicalRegexpVisitor() {}
  virtual ~PhysicalRegexpVisitor() {}

  ret_type Visit(Regexp* regexp) {
    switch (regexp->type()) {
#define TYPE_CASE(RegexpType)                                                  \
      case k##RegexpType:                                                      \
        return Visit##RegexpType(reinterpret_cast<RegexpType*>(regexp));       \
        break;
      LIST_PHYSICAL_REGEXP_TYPES(TYPE_CASE)
#undef TYPE_CASE
      default:
        UNREACHABLE();
        return static_cast<ret_type>(0);
    }
  }

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual ret_type Visit##RegexpType(RegexpType* r) = 0;
  LIST_PHYSICAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

 private:
  DISALLOW_COPY_AND_ASSIGN(PhysicalRegexpVisitor);
};


typedef bool (*MatchFullFunc)(const char*, size_t);
typedef bool (*MatchAnywhereFunc)(const char*, size_t);
typedef bool (*MatchFirstFunc)(const char*, size_t, Match*);
typedef unsigned (*MatchAllFunc)(const char*, size_t, std::vector<Match>*);

class RegexpInfo {
 public:
  RegexpInfo()
    : regexp_(NULL),
      entry_state_(-1), output_state_(-1), last_state_(0),
      regexp_max_length_(0),
      match_full_(NULL),
      match_anywhere_(NULL),
      match_first_(NULL),
      match_all_(NULL),
      vmem_match_full_(NULL),
      vmem_match_anywhere_(NULL),
      vmem_match_first_(NULL),
      vmem_match_all_(NULL) {}
  ~RegexpInfo();

  void set_regexp(Regexp* regexp) { regexp_ = regexp; }
  Regexp* regexp() const { return regexp_; }
  void UpdateRegexpMaxLength(Regexp* regexp) {
    regexp_max_length_ = max(regexp_max_length_,  regexp->MatchLength());
  }


  // Accessors.
  int entry_state() const { return entry_state_; }
  int output_state() const { return output_state_; }
  int last_state() const { return last_state_; }
  unsigned regexp_max_length() const { return regexp_max_length_; }
  void set_entry_state(int entry_state) { entry_state_ = entry_state; }
  void set_output_state(int output_state) { output_state_ = output_state; }
  void set_last_state(int last_state) { last_state_ = last_state; }
  void set_regexp_max_length(unsigned regexp_max_length) {
    regexp_max_length_ = regexp_max_length;
  }

  vector<Regexp*>* regexp_tree() { return &regexp_tree_; }
  vector<Regexp*>* ff_list() { return &ff_list_; }
  vector<Regexp*>* gen_list() { return &gen_list_; }
  vector<Regexp*>* extra_allocated() { return &extra_allocated_; }

 private:
  Regexp* regexp_;
  int entry_state_;
  int output_state_;
  int last_state_;
  unsigned regexp_max_length_;
  vector<Regexp*> regexp_tree_;
  vector<Regexp*> ff_list_;
  vector<Regexp*> gen_list_;
  // This is used to store regexp allocated later than parsing time, and hence
  // not present in the regexp tree (which root is regexp_).
  vector<Regexp*> extra_allocated_;

 private:
  // The compiled functions.
  MatchFullFunc match_full_;
  MatchAnywhereFunc match_anywhere_;
  MatchFirstFunc match_first_;
  MatchAllFunc match_all_;
  // Their associated virtual memory.
  VirtualMemory* vmem_match_full_;
  VirtualMemory* vmem_match_anywhere_;
  VirtualMemory* vmem_match_first_;
  VirtualMemory* vmem_match_all_;

  DISALLOW_COPY_AND_ASSIGN(RegexpInfo);

  friend Regej;
};


// Regexp utils ----------------------------------------------------------------

bool regexp_cmp_entry_state(Regexp* r1, Regexp* r2);
bool regexp_cmp_output_state(Regexp* r1, Regexp* r2);

bool all_regexps_start_at(int entry_state, vector<Regexp*> *regexps);


} }  // namespace rejit::internal

#endif  // REJIT_REGEXP_H_

