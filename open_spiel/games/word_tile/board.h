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

#ifndef OPEN_SPIEL_GAMES_WORD_TILE_BOARD_H_
#define OPEN_SPIEL_GAMES_WORD_TILE_BOARD_H_

#include <array>
#include <vector>

#include "open_spiel/games/word_tile/word_tile.h"

namespace open_spiel {
namespace word_tile {

// Standard Scrabble 15x15 premium layout.
// Coordinates 0-indexed top-left (0,0) = A15 top left in usual notation,
// but we will map to our internal 0 top.
// Premium enum from word_tile.h
inline std::vector<std::vector<Premium>> CreateStandardBoard() {
  const int N = 15;
  std::vector<std::vector<Premium>> board(N, std::vector<Premium>(N, Premium::kNormal));
  auto set = [&](int r, int c, Premium p) { board[r][c] = p; };
  // Triple Word scores (corners and mid edges) – standard positions 0-indexed
  const std::vector<std::pair<int,int>> TW = {{0,0},{0,7},{0,14},{7,0},{7,14},{14,0},{14,7},{14,14}};
  for (auto [r,c] : TW) set(r,c,Premium::kTripleWord);
  // Double Word – diagonal pattern plus center star treated separately but we mark center as DW initially then override star logic
  const std::vector<std::pair<int,int>> DW = {{1,1},{2,2},{3,3},{4,4},{1,13},{2,12},{3,11},{4,10},{13,1},{12,2},{11,3},{10,4},{13,13},{12,12},{11,11},{10,10},{7,7}};
  for (auto [r,c] : DW) set(r,c,Premium::kDoubleWord);
  // Triple Letter
  const std::vector<std::pair<int,int>> TL = {{1,5},{1,9},{5,1},{5,5},{5,9},{5,13},{9,1},{9,5},{9,9},{9,13},{13,5},{13,9}};
  for (auto [r,c] : TL) set(r,c,Premium::kTripleLetter);
  // Double Letter
  const std::vector<std::pair<int,int>> DL = {{0,3},{0,11},{2,6},{2,8},{3,0},{3,7},{3,14},{6,2},{6,6},{6,8},{6,12},{7,3},{7,11},{8,2},{8,6},{8,8},{8,12},{11,0},{11,7},{11,14},{12,6},{12,8},{14,3},{14,11}};
  for (auto [r,c] : DL) set(r,c,Premium::kDoubleLetter);
  return board;
}

}  // namespace word_tile
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_WORD_TILE_BOARD_H_
