// Copyright 2026 Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "open_spiel/games/word_tile/dictionary.h"

#include <fstream>
#include <cctype>
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace word_tile {

namespace {
std::string Normalize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (std::isalpha(c)) out.push_back(std::toupper(c));
  }
  return out;
}
}  // namespace

Dictionary::Dictionary(const std::string& path) {
  std::ifstream fin(path);
  if (!fin.is_open()) {
    SpielFatalError("Could not open dictionary file: " + path);
  }
  std::string line;
  while (std::getline(fin, line)) {
    std::string w = Normalize(line);
    if (!w.empty()) words_.insert(w);
  }
}

bool Dictionary::IsValidWord(const std::string& word) const {
  return words_.find(Normalize(word)) != words_.end();
}

TrieDictionary::TrieDictionary(const std::string& path) : root_(std::make_unique<TrieNode>()) {
  std::ifstream fin(path);
  if (!fin.is_open()) {
    SpielFatalError("Could not open dictionary file: " + path);
  }
  std::string line;
  while (std::getline(fin, line)) {
    std::string w = Normalize(line);
    if (w.empty()) continue;
    TrieNode* node = root_.get();
    for (char c : w) {
      int idx = c - 'A';
      if (idx < 0 || idx >= 26) { node = nullptr; break; }
      if (!node->children[idx]) node->children[idx] = std::make_unique<TrieNode>();
      node = node->children[idx].get();
    }
    if (node) node->is_word = true;
  }
}

bool TrieDictionary::IsValidWord(const std::string& word) const {
  std::string w = Normalize(word);
  const TrieNode* node = root_.get();
  for (char c : w) {
    int idx = c - 'A';
    if (idx < 0 || idx >= 26 || !node->children[idx]) return false;
    node = node->children[idx].get();
  }
  return node && node->is_word;
}

bool TrieDictionary::IsPrefix(const std::string& prefix) const {
  std::string w = Normalize(prefix);
  const TrieNode* node = root_.get();
  for (char c : w) {
    int idx = c - 'A';
    if (idx < 0 || idx >= 26 || !node->children[idx]) return false;
    node = node->children[idx].get();
  }
  return true;
}

}  // namespace word_tile
}  // namespace open_spiel
