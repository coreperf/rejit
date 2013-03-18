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

#include "globals.h"
#include "parser.h"
#include "macro-assembler.h"

namespace rejit {
namespace internal {

// Called from the generated code to register a match.
void RegisterMatch(vector<Match>* matches, Match new_match);


class NB_RegexpIndexer : public RealRegexpVisitor<void> {
 public:
  explicit NB_RegexpIndexer(RegexpInfo* rinfo,
                            int entry_state = 0,
                            int last_state = 0)
    : rinfo_(rinfo), entry_state_(entry_state), last_state_(last_state) {}

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual void Visit##RegexpType(RegexpType* r);
  LIST_REAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS
  void VisitRegexp(Regexp* re);

  
  void Index(Regexp* regexp);
  // By default index from 0 and create the output state.
  // If specified force the entry and/or output states.
  void IndexSub(Regexp* regexp, int entry_state = 0, int output_state = -1);

  RegexpInfo* rinfo() const { return rinfo_; }
  int entry_state() const { return entry_state_; }
  int last_state() const { return last_state_; }

 private:
  RegexpInfo* rinfo_;
  int entry_state_;
  int last_state_;
};



class NB_RegexpLister : public RealRegexpVisitor<void> {
 public:
  explicit NB_RegexpLister(RegexpInfo* rinfo, vector<Regexp*>* physical_regexp_list) :
    rinfo_(rinfo),
    physical_regexp_list_(physical_regexp_list) {}

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual void Visit##RegexpType(RegexpType* r);
  LIST_REAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS
  void VisitRegexp(Regexp* re);

  void List(Regexp* re) {
    physical_regexp_list_->push_back(re);
  }

  // List a regexp allocated by the lister.
  // Remember it in the RegexpInfo to correctly delete it later.
  void ListNew(Regexp* re) {
    rinfo_->extra_allocated()->push_back(re);
    List(re);
  }

  RegexpInfo* rinfo() const { return rinfo_; }

 private:
  RegexpInfo* rinfo_;
  vector<Regexp*>* physical_regexp_list_;

  DISALLOW_COPY_AND_ASSIGN(NB_RegexpLister);
};


class FF_finder : public RealRegexpVisitor<bool> {
 public:
  FF_finder(Regexp* root, vector<Regexp*>* regexp_list) :
    root_(root), regexp_list_(regexp_list) {}

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual bool Visit##RegexpType(RegexpType* r);
  LIST_REAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

    int ff_cmp(size_t i1,
               size_t i2,
               size_t i3);

 private:
  Regexp* root_;
  vector<Regexp*>* regexp_list_;
  DISALLOW_COPY_AND_ASSIGN(FF_finder);
};


class NB_Codegen : public PhysicalRegexpVisitor<void> {
 public:
  NB_Codegen();

  enum Direction {
    kForward,
    kBackward
  };

  VirtualMemory* Compile(RegexpInfo* rinfo, MatchType match_type);

  void Generate(RegexpInfo* rinfo, MatchType match_type);

  bool GenerateFastForward(RegexpInfo* rinfo, MatchType match_type);

  void HandleControlRegexps(RegexpInfo* rinfo);

  void CheckMatch(Direction direction,
                  RegexpInfo* rinfo,
                  MatchType match_type,
                  Label* limit,
                  Label* match);
  void GenerateMatchDirection(Direction direction,
                              RegexpInfo* rinfo,
                              MatchType match_type,
                              Label* fast_forward = NULL);
  inline void GenerateMatchBackward(RegexpInfo* rinfo,
                                    MatchType match_type,
                                    Label* fast_forward = NULL) {
    GenerateMatchDirection(kBackward, rinfo, match_type, fast_forward);
  }
  inline void GenerateMatchForward(RegexpInfo* rinfo,
                                   MatchType match_type,
                                   Label* fast_forward = NULL) {
    GenerateMatchDirection(kForward, rinfo, match_type, fast_forward);
  }

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  virtual void Visit##RegexpType(RegexpType* r);
  LIST_PHYSICAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

  void Advance(unsigned n_chars = 1);

  void TestState(int time, int state_index);
  void SetState(int target_time, int target_index, int current_index);
  // Set target state with the current string_pointer as the match source.
  void SetStateForce(int target_time, int target_index);
  void SetStateForce(int target_time, Register target_index);

  // Direction helpers.
  void DirectionTestEntryState(int time, Regexp* regexp);
  void DirectionSetOutputFromEntry(int time, Regexp* regexp);

  // Only use if certain that the access will not overflow the ring_state.
  // Typically with time == 0.
  Operand StateOperand(int time, int state_index);
  Operand StateOperand(int time, Register state_index);
  Operand StateOperand(Register offset);
  void ComputeStateOperandOffset(Register offset, int time, int index);


  void ClearTime(int time);
  void ClearAllTimes();
  int TimeSummaryBaseOffsetFromFrame();
  Operand TimeSummaryOperand(int time);
  Operand TimeSummary(int offset);

  Operand StateRingBase();
  int StateRingBaseOffsetFromFrame();

  Operand result_matches();

  void FlowTime();
  void CheckTimeFlow();


  // Accessors.
  MacroAssembler* masm() const { return masm_; }
  MatchType match_type() const { return match_type_; }
  bool ff_pre_scan() const { return false && ff_pre_scan_; }
  int state_ring_time_size() const { return state_ring_time_size_; }
  int state_ring_times() const { return state_ring_times_; }
  int state_ring_size() const { return state_ring_size_; }
  int time_summary_size() const { return time_summary_size_; }
  Direction direction() const { return direction_; }

  void set_direction(Direction dir);

 private:
  MacroAssembler* masm_;
  MatchType match_type_;
  bool ff_pre_scan_;

  // The size in bytes of a time of the ring state.
  int state_ring_time_size_;
  // The number of times in the ring state.
  int state_ring_times_;
  // The total size (in bytes) of the ring state.
  int state_ring_size_;
  int time_summary_size_;
  Operand ring_base_;

  Direction direction_;
};


class NB_FastForwardGen {
 public:
  NB_FastForwardGen(NB_Codegen* codegen, vector<Regexp*>* list) :
    codegen_(codegen),
    masm_(codegen->masm()),
    regexp_list_(list),
    potential_match_(NULL) {}

  void Generate();

  void FoundState(int time, int state);

#define DECLARE_SINGLE_REGEXP_VISITORS(RegexpType) \
  void VisitSingle##RegexpType(RegexpType* r);
  LIST_PHYSICAL_REGEXP_TYPES(DECLARE_SINGLE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

  void VisitSingle(Regexp* regexp) {
    switch (regexp->type()) {
#define TYPE_CASE(RegexpType)                                             \
      case k##RegexpType:                                                 \
        VisitSingle##RegexpType(reinterpret_cast<RegexpType*>(regexp));   \
        break;
      LIST_PHYSICAL_REGEXP_TYPES(TYPE_CASE)
#undef TYPE_CASE
      default:
        UNREACHABLE();
    }
  }

#define DECLARE_REGEXP_VISITORS(RegexpType) \
  void Visit##RegexpType(RegexpType* r);
  LIST_PHYSICAL_REGEXP_TYPES(DECLARE_REGEXP_VISITORS)
#undef DECLARE_REGEXP_VISITORS

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

 private:
  NB_Codegen* codegen_;
  MacroAssembler* masm_;
  vector<Regexp*>* regexp_list_;
  Label* potential_match_;

  DISALLOW_COPY_AND_ASSIGN(NB_FastForwardGen);
};


// TODO(rames): Explain.
} }  // namespace rejit::internal

