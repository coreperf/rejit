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

#ifndef REJIT_AUTOMATA_H_
#define REJIT_AUTOMATA_H_

#include "globals.h"
#include "platform.h"

namespace rejit {
namespace internal {

// Physical regular expression types directly match characters.
// Order matters. See aliases below.
// TODO(rames): re-classify regexps.
#define LIST_PHYSICAL_REGEXP_TYPES(M)                                          \
  M(MultipleChar)                                                              \
  M(Period)                                                                    \
  M(Bracket)                                                                   \
  M(StartOfLine)                                                               \
  M(EndOfLine)                                                                 \
  M(Epsilon)

#define LIST_ABSTRACT_REGEXP_TYPES(M)                                          \
  M(Concatenation)                                                             \
  M(Repetition)                                                                \
  M(Alternation)
// Real regular expression types, ie. types that can appear in a regular
// expression tree, after parsing has finished and succeded.
// There is a sub class of Regexp for each of these.
#define LIST_REAL_REGEXP_TYPES(M)                                              \
  LIST_PHYSICAL_REGEXP_TYPES(M)                                                \
  LIST_ABSTRACT_REGEXP_TYPES(M)
// Virtual regular expression types. They are only used at parsing time.
// No matching classes exist for these.
#define LIST_VIRTUAL_REGEXP_TYPES(M)                                           \
  M(LeftParenthesis)                                                           \
  M(AlternateBar)
// Left parenthesis and alternate bar must be defined last, as
// Regexp::IsMarker depends on it.
#define LIST_REGEXP_TYPES(M)                                                   \
  LIST_REAL_REGEXP_TYPES(M)                                                    \
  LIST_VIRTUAL_REGEXP_TYPES(M)


// Enumerate types tokens.
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
// TODO(rames): This should probably be computed at runtime to match some
// architectural limit related to the caches.
static const unsigned kMaxNodeLength = 64;


// Regexps ---------------------------------------------------------------------

// This is the base class for Regexps.
// The parser builds a tree of Regexps, that is then passed to a code generator
// to generate a some code matching the respresented regular expression.
class Regexp {
 public:
  explicit Regexp(RegexpType type) :
    type_(type), entry_state_(-1), output_state_(-1) {}
  virtual ~Regexp() {}
  virtual Regexp* DeepCopy();

#define DECLARE_IS_REGEXP_HELPERS(RegexpType)                                  \
  bool Is##RegexpType() { return type_ == k##RegexpType; }
  LIST_REGEXP_TYPES(DECLARE_IS_REGEXP_HELPERS)
#undef DECLARE_IS_REGEXP_HELPERS

  inline bool IsControlRegexp() {
    return kFirstControlRegexp <= type() && type() <= kLastControlRegexp;
  }

  inline bool IsPhysical() {
    return type() <= kLastPhysicalRegexp;
  }

#define DECLARE_CAST(RegexpType)                                               \
  RegexpType* As##RegexpType() {                                               \
    ASSERT(Is##RegexpType());                                                  \
    return reinterpret_cast<RegexpType*>(this);                                \
  }
  LIST_REAL_REGEXP_TYPES(DECLARE_CAST)
#undef DECLARE_CAST

  // Left parenthesis and vertical bar are markers for the parser.
  inline bool IsMarker() const { return type_ >= kFirstMarker; }

  // The maximum number of characters matched by this regexp.
  // This is used to determine how many times must be allocated for the state
  // ring.
  // TODO(rames): Should we distinguish the match length when known for certain.
  virtual unsigned MatchLength() const { return 0; }

  virtual void SetEntryState(int entry_state);
  virtual void SetOutputState(int output_state);

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


// TODO(rames): Implementation of MC assumes that the regexp stays available for
// the lifetime of the generated functions.
class MultipleChar : public Regexp {
 public:
  MultipleChar(const char* chars, unsigned count);
  virtual Regexp* DeepCopy();

  inline bool IsFull() {
    ASSERT(chars_length_ <= kMaxNodeLength);
    return chars_length_ == kMaxNodeLength;
  }
  void PushChar() {
    ASSERT(!IsFull());
    chars_length_++;
  }

  virtual unsigned MatchLength() const { return chars_length(); }

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  const char* chars() const { return chars_; }
  unsigned chars_length() const { return chars_length_; }

  int64_t first_chars() const {
    return *reinterpret_cast<const int64_t*>(chars_) &
      FirstCharsMask(chars_length());
  }

 protected:
  const char* chars_;
  unsigned chars_length_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultipleChar);
};


class Period : public Regexp {
 public:
  Period() : Regexp(kPeriod) {}
  virtual Regexp* DeepCopy() { return new Period(); }
  virtual unsigned MatchLength() const { return 1; }

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

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  void AddSingleChar(char c) { single_chars_.push_back(c); }
  void AddCharRange(CharRange range) { char_ranges_.push_back(range); }

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


// Control regexp don't match characters (match length of 0). They check for
// conditions or have side effects.
class ControlRegexp : public Regexp {
 protected:
  explicit ControlRegexp(RegexpType type) : Regexp(type) {}

  // Control regexp never match physical characters.
  virtual unsigned MatchLength() const { return 0; }
  
};


class StartOfLine : public ControlRegexp {
 public:
  StartOfLine() : ControlRegexp(kStartOfLine) {}
  virtual Regexp* DeepCopy();

 private:
  DISALLOW_COPY_AND_ASSIGN(StartOfLine);
};


class EndOfLine : public ControlRegexp {
 public:
  EndOfLine() : ControlRegexp(kEndOfLine) {}
  virtual Regexp* DeepCopy();

 private:
  DISALLOW_COPY_AND_ASSIGN(EndOfLine);
};


class Epsilon : public ControlRegexp {
 public:
  explicit Epsilon(int entry, int output)
    : ControlRegexp(kEpsilon) {
      entry_state_ = entry;
      output_state_ = output;
    }

 private:
  DISALLOW_COPY_AND_ASSIGN(Epsilon);
};


// Convenience base classes for regular expressions with sub regular expressions.

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

  void Append(Regexp*);

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
  virtual Regexp* DeepCopy();

  // Accessors.
  Regexp* sub_regexp() { return sub_regexp_; }

 protected:
  Regexp* sub_regexp_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegexpWithOneSub);
};


// TODO(rames): The current handling of repetitions is really poor. See code
// generation.
class Repetition : public RegexpWithOneSub {
 public:
  Repetition(Regexp* sub_regexp, uint32_t min_rep, uint32_t max_rep)
    : RegexpWithOneSub(kRepetition, sub_regexp),
      min_rep_(min_rep),
      max_rep_(max_rep) {}
  virtual Regexp* DeepCopy();
  // TODO(rames): We are leaking regexps allocated at code generation time.

  virtual unsigned MatchLength() const { return sub_regexp_->MatchLength(); }

  virtual ostream& OutputToIOStream(ostream& stream) const;  // NOLINT

  virtual void SetEntryState(int entry_state);
  virtual void SetOutputState(int output_state);

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

// TODO(rames): This is a quick implementation based on the types of the
// regexps. Should I use a visitor pattern based on virtual methods instead?
// Investigate.

template <class ret_type> class RegexpVisitor {
 public:
  RegexpVisitor() {}
  virtual ~RegexpVisitor() {}

  ret_type Visit(Regexp* regexp) {
    switch (regexp->type()) {
#define TYPE_CASE(RegexpType)                                       \
      case k##RegexpType:                                           \
        return Visit##RegexpType(reinterpret_cast<RegexpType*>(regexp));   \
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
  DISALLOW_COPY_AND_ASSIGN(RegexpVisitor);
};


// A visitor for physical regular expressions only.
class PhysicalRegexpVisitor {
 public:
  PhysicalRegexpVisitor() {}
  virtual ~PhysicalRegexpVisitor() {}

  void Visit(Regexp* regexp) {
    switch (regexp->type()) {
#define TYPE_CASE(RegexpType)                                       \
      case k##RegexpType:                                           \
        Visit##RegexpType(reinterpret_cast<RegexpType*>(regexp));   \
        break;
      LIST_PHYSICAL_REGEXP_TYPES(TYPE_CASE)
#undef TYPE_CASE
      default:
        UNREACHABLE();
    }
  }

#define DECLARE_UNREACHABLE_VISITORS(RegexpType) \
  void Visit##RegexpType(RegexpType* r) { UNREACHABLE(); }
  LIST_ABSTRACT_REGEXP_TYPES(DECLARE_UNREACHABLE_VISITORS)
#undef DECLARE_REGEXP_VISITORS

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual void Visit##RegexpType(RegexpType* r) = 0;
  LIST_PHYSICAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

 private:
  DISALLOW_COPY_AND_ASSIGN(PhysicalRegexpVisitor);
};


typedef void (*JitFunc)(const char*);
typedef bool (*MatchFullFunc)(const char*);
typedef bool (*MatchAnywhereFunc)(const char*);
typedef bool (*MatchFirstFunc)(const char*, Match*);
typedef unsigned (*MatchAllFunc)(const char*, std::vector<Match>*);

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

  // TODO(rames): set to private
 public:
  // The compiled functions.
  MatchFullFunc match_full_;
  MatchAnywhereFunc match_anywhere_;
  MatchFirstFunc match_first_;
  MatchAllFunc match_all_;
  // Their associated virtual memory.
  // TODO(rames): Clean that.
  VirtualMemory* vmem_match_full_;
  VirtualMemory* vmem_match_anywhere_;
  VirtualMemory* vmem_match_first_;
  VirtualMemory* vmem_match_all_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegexpInfo);
};


// Regexp utils ----------------------------------------------------------------

// If there is a single regexp with the given entry state return it.
// Else return NULL.
Regexp* single_re_at_entry(const vector<Regexp*>* list, int entry);

// Order by entry state and type.
bool cmp_entry_state_type(Regexp* r1, Regexp* r2);

// Preferred order for fast forward selection.
int ff_phy_cmp(Regexp* r1, Regexp* r2);


} }  // namespace rejit::internal

#endif  // REJIT_AUTOMATA_H_

