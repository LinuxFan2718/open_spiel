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

#ifndef OPEN_SPIEL_GAMES_WORD_TILE_DICTIONARY_H_
#define OPEN_SPIEL_GAMES_WORD_TILE_DICTIONARY_H_

#include <string>
#include <unordered_set>
#include <memory>

namespace open_spiel {
namespace word_tile {

class Dictionary {
 public:
  explicit Dictionary(const std::string& path);
  bool IsValidWord(const std::string& word) const;
  size_t size() const { return words_.size(); }

 private:
  std::unordered_set<std::string> words_;
};

// Simple trie node for prefix pruning during move generation (future optimization)
struct TrieNode {
  bool is_word = false;
  std::array<std::unique_ptr<TrieNode>, 26> children{};
};

class TrieDictionary {
 public:
  explicit TrieDictionary(const std::string& path);
  bool IsValidWord(const std::string& word) const;
  bool IsPrefix(const std::string& prefix) const;
 private:
  std::unique_ptr<TrieNode> root_;
};

}  // namespace word_tile
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_WORD_TILE_DICTIONARY_H_
