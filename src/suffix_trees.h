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


// Simple suffix tree implementation. This will be used to try to simplify
// alternations of strings in fast-forward elements.
// TODO: The current implementation always insert starting from the root.
// Improve the implementation, maybe using suffix pointers (Ukkonen's
// algorithm).

#ifndef REJIT_SUFFIX_TREES_H_
#define REJIT_SUFFIX_TREES_H_

#include <map>
#include <set>
#include <string>

#include "regexp.h"

using namespace std;

namespace rejit {
namespace internal {


class SuffixTree {
 public:
  explicit SuffixTree() :
      str_(NULL), str_start_(-1), str_end_(-1), active_length_(0) {}
  // `parent_active_length` is the active length of the parent node to which
  // this node is being appended.
  explicit SuffixTree(const char *str, int start, int end,
                      int parent_active_length = 0) :
      str_(str), str_start_(start), str_end_(end),
      active_length_(parent_active_length + end - start) {}
  virtual ~SuffixTree();

  inline string edge_string() {
    return string(str_, str_start_, str_end_ - str_start_);
  }
  inline char first_edge_char() { return str_[str_start_]; }

  virtual ostream& OutputToIOStream(ostream& stream) const;
  void print();

  // Accessors.
  const char *str() const { return str_; }
  const int str_start() const { return str_start_; }
  const int str_end() const { return str_end_; }

  inline const map<char,SuffixTree*> *suffixes() const { return &suffixes_; }
  inline const int active_length() const { return active_length_; }

 private:
  // Edges leading to suffix nodes.
  map<char, SuffixTree*> suffixes_;
  // Characters for the edge leading to this node.
  const char *str_;
  int str_start_;
  int str_end_;
  // The set of strings fow which this node is a suffix.
  set<const char*> terminated_strings_;

  int active_length_;

  friend class SuffixTreeBuilder;
};


// Overload the '<<' operator to allow convenient printing to an iostream.
inline ostream& operator<<(ostream& stream, const SuffixTree& suffix_tree) {
  suffix_tree.OutputToIOStream(stream);
  return stream;
}


class SuffixTreeBuilder {
 public:
  SuffixTreeBuilder() :
      root_(new SuffixTree()), active_length_(0) {
    active_node_ = root_;
  }
  ~SuffixTreeBuilder() { delete root_; }

  // From the active node, walk toward the node at which we must perform the
  // next suffix insertion (which will become the new active node).
  // Returns the offset at which the insertion must be done on the current edge.
  int walk_down(const char *str);

  // Build the suffixes for this string into the existing suffix tree.
  SuffixTree *append_string(const char *str);

  inline const SuffixTree *root() const { return root_; }

 private:
  SuffixTree *root_;
  SuffixTree *active_node_;
  int active_length_;
};


} }  // namespace rejit::internal

#endif
