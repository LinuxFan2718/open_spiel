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

#ifndef OPEN_SPIEL_GAMES_WORD_TILE_TILES_H_
#define OPEN_SPIEL_GAMES_WORD_TILE_TILES_H_

#include <array>
#include <string>

namespace open_spiel {
namespace word_tile {

// English Scrabble tile distribution from standard rules.
// Index 0-25 = A-Z, 26 = blank '?'
inline constexpr std::array<int, 27> kTileDistribution = {
    9,  // A
    2,  // B
    2,  // C
    4,  // D
    12, // E
    2,  // F
    3,  // G
    2,  // H
    9,  // I
    1,  // J
    1,  // K
    4,  // L
    2,  // M
    6,  // N
    8,  // O
    2,  // P
    1,  // Q
    6,  // R
    4,  // S
    6,  // T
    4,  // U
    2,  // V
    2,  // W
    1,  // X
    2,  // Y
    1,  // Z
    2   // blank
};

inline constexpr std::array<int, 27> kLetterValues = {
    1,  // A
    3,  // B
    3,  // C
    2,  // D
    1,  // E
    4,  // F
    2,  // G
    4,  // H
    1,  // I
    8,  // J
    5,  // K
    1,  // L
    3,  // M
    1,  // N
    1,  // O
    3,  // P
    10, // Q
    1,  // R
    1,  // S
    1,  // T
    1,  // U
    4,  // V
    4,  // W
    8,  // X
    4,  // Y
    10, // Z
    0   // blank
};

inline constexpr int kTotalTiles = 100;
inline constexpr int kBingoBonus = 50;

inline int LetterToIndex(char c) {
  if (c == '?' || c == ' ') return 26;
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  if (c >= 'A' && c <= 'Z') return c - 'A';
  return -1;
}

inline char IndexToLetter(int idx) {
  if (idx == 26) return '?';
  return static_cast<char>('A' + idx);
}

inline int LetterValue(char c) {
  int idx = LetterToIndex(c);
  if (idx < 0 || idx >= 27) return 0;
  return kLetterValues[idx];
}

}  // namespace word_tile
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_WORD_TILE_TILES_H_
