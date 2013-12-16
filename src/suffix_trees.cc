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

#include "suffix_trees.h"

#include "utils.h"


namespace rejit {
namespace internal {


SuffixTree::~SuffixTree() {
  for (pair<char, SuffixTree*> edge : suffixes_) {
    delete edge.second;
  }
}

ostream& SuffixTree::OutputToIOStream(ostream& stream) const {
  if (str_start_ >= 0 && str_end_ >= 0) {
    Indent(stream) << string(str_, str_start_, str_end_ - str_start_);
  }

  if (terminated_strings_.size()) {
    cout << "\t(terminates strings:";
    for (const char *s : terminated_strings_) {
      cout << " \"" << s << "\"";
    }
    cout << ")";
  }
  cout << endl;

  {
    // Align the suffixes after the end of the string for this node.
    IndentScope is(str_end_ - str_start_);
    for (pair<char, SuffixTree*> edge : suffixes_) {
      cout << *(edge.second);
    }
  }
  return stream;
}


// This cannot be declared inline as the '<<' operator has not been overloaded
// before the declaration site.
void SuffixTree::print() { cout << *this; }


// TODO: Is there no standard library function for this?!
static size_t str_diff_index(const char *s1, const char *s2) {
  size_t i = 0;
  while (s1[i] && s2[i] && s1[i] == s2[i]) {
    i++;
  }
  return i;
}


int SuffixTreeBuilder::walk_down(const char *str) {
  size_t str_len = strlen(str);
  map<char,SuffixTree*>::iterator edge_it;
  SuffixTree *edge_node;
  string edge_str;
  size_t diff_index = 0;

  while (active_length_ < str_len &&
         active_node_->suffixes_.end() !=
         (edge_it = active_node_->suffixes_.find(str[active_length_]))) {
    edge_node = (*edge_it).second;
    edge_str = edge_node->edge_string();
    diff_index = str_diff_index(edge_str.c_str(), str + active_length_);

    if (diff_index == edge_str.length()) {
      // We consumed the whole edge to go to the next node.
      active_node_ = (*edge_it).second;
      active_length_ += diff_index;
      diff_index = 0;
      continue;
    } else {
      // Stay on the current active node. 'diff_index' indicates at what point a
      // split is necessary.
      return diff_index;
    }
  }

  return diff_index;
}


SuffixTree *SuffixTreeBuilder::append_string(const char *str) {
  size_t str_len = strlen(str);
  size_t offset;

  for (int i = 0; i < str_len; i++) {
#ifdef TRACE_SUFFIX_TREES_CONSTRUCTION
    cout << "--------------------" << endl;
    cout << "inserting " << str + i << endl << endl;;
#endif

    // TODO: We currently only insert from the root. Usage of suffix pointers
    // will be added in a later commit.
    active_node_ = root_;
    active_length_ = 0;

    offset = walk_down(str + i);

    if (offset) {

      // We need to split along the current edge.
      map<char,SuffixTree*>::iterator active_edge_it =
        active_node_->suffixes_.find(str[i + active_length_]);
      SuffixTree *edge_node = (*active_edge_it).second;

      // Split the existing edge in two.
      SuffixTree *middle_node =
        new SuffixTree(str, i + active_length_, i + active_length_ + offset,
                       active_length_);
      active_node_->suffixes_[str[i + active_length_]] = middle_node;
      edge_node->str_start_ = edge_node->str_start_ + offset;
      middle_node->suffixes_[edge_node->first_edge_char()] = edge_node;

      if (i + active_length_ + offset < str_len) {
        // Create a new node for the remaining characters in the string.
        SuffixTree *new_node =
          new SuffixTree(str, i + active_length_ + offset, str_len,
                         active_length_);
        middle_node->suffixes_[str[i + active_length_ + offset]] = new_node;
        new_node->terminated_strings_.insert(str);
      } else {
        // The string ends on this middle node.
        middle_node->terminated_strings_.insert(str);
      }

    } else {
      if (i + active_length_ == str_len) {
        // No more characters to add.
        active_node_->terminated_strings_.insert(str);
      } else {
        SuffixTree *new_node =
          new SuffixTree(str, i + active_length_, str_len, active_length_);
        active_node_->suffixes_[str[i + active_length_]] = new_node;
        new_node->terminated_strings_.insert(str);
      }
    }

#ifdef TRACE_SUFFIX_TREES_CONSTRUCTION
    cout << *root_;
    cout << "--------------------" << endl;
#endif
  }

  return root_;
}


} }  // namespace rejit::internal
