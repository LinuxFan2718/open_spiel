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

#ifndef OPEN_SPIEL_GAMES_WORD_TILE_WORD_TILE_H_
#define OPEN_SPIEL_GAMES_WORD_TILE_WORD_TILE_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/container/flat_hash_map.h"
#include "open_spiel/abseil-cpp/absl/types/optional.h"
#include "open_spiel/abseil-cpp/absl/types/span.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/utils/status.h"

// Word Tile Game – two-player word board game inspired by Scrabble.
// Implements standard 15x15 board, 100 tile English distribution, 7-tile rack,
// double challenge rule, imperfect information (opponent rack hidden).
//
// This game uses action structs only, similar to crossword game.
// See spiel.h GameType.action_structs_only.
//
// Parameters:
//   dictionary_file string  Path to word list, one word per line uppercase.
//                           Default points to shipped ENABLE list.
//   board_size      int     Board size, default 15, fixed for now.
//   rack_size       int     Rack size default 7.

namespace open_spiel {
namespace word_tile {

inline constexpr int kNumPlayers = 2;
inline constexpr int kDefaultBoardSize = 15;
inline constexpr int kDefaultRackSize = 7;
inline constexpr int kNumTileTypes = 27;  // A-Z plus blank
inline constexpr int kNumPremiumTypes = 5; // normal, DL, TL, DW, TW
inline constexpr int kMaxGameLength = 2000; // generous upper bound

enum class Premium : uint8_t {
  kNormal = 0,
  kDoubleLetter = 1,
  kTripleLetter = 2,
  kDoubleWord = 3,
  kTripleWord = 4,
};

enum class Phase : uint8_t {
  kDealInitial = 0,      // chance node to deal initial racks
  kPlay = 1,             // decision node: current player chooses Place/Exchange/Pass
  kChallenge = 2,        // decision node: opponent chooses Challenge or Accept after a Place
  kDraw = 3,             // chance node to draw replacement tiles after Place or Exchange
  kGameOver = 4,
};

struct WordTileActionStruct : public ActionStruct {
  // type: "place", "exchange", "pass", "challenge", "accept"
  std::string type;
  // for place:
  int start_row = -1;
  int start_col = -1;
  int direction = 0; // 0 horizontal, 1 vertical
  std::string word; // full word string including existing board letters, uppercase A-Z
  std::string tiles_played; // pattern of tiles played from rack, '.' for existing board letters, '?' for blank, letters for normal tiles played
  std::string blank_assignments; // e.g. "2:J,5:S" positions within word (0-index) assigned to blank, empty if none
  // for exchange:
  std::string exchange_tiles; // e.g. "AEIOU?" up to rack size, '?' for blank

  SPIEL_STRUCT_BOILERPLATE(WordTileActionStruct, type, start_row, start_col,
                           direction, word, tiles_played, blank_assignments,
                           exchange_tiles);
};

class WordTileGame;

class WordTileState : public State {
 public:
  explicit WordTileState(std::shared_ptr<const Game> game);
  WordTileState(const WordTileState&) = default;

  Player CurrentPlayer() const override;
  std::string ActionToString(Player player, Action action) const override;
  std::string ToString() const override;
  bool IsTerminal() const override;
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  void UndoAction(Player player, Action action) override;
  std::vector<Action> LegalActions() const override;
  bool IsChanceNode() const override;
  std::vector<std::pair<Action, double>> ChanceOutcomes() const override;

  // Action struct interface – required because GameType sets action_structs_only=true
  std::unique_ptr<ActionStruct> ActionToStruct(Player player,
                                               Action action_id) const override;
  std::vector<Action> StructToActions(const ActionStruct& action_struct) const override;
  Status ApplyActionStruct(const ActionStruct& action_struct) override;
  Status ValidateActionStruct(const ActionStruct& action_struct) const override;
  std::unique_ptr<ActionStructSampler> GetActionStructSampler(
      int seed) const override;

  std::string InformationStateTensorShape() const { return ""; } // placeholder

 protected:
  void DoApplyAction(Action action) override;
  void DoApplyAction(const WordTileActionStruct& a);

 private:
  void InitializeBoardPremiums();
  void InitializeTileBag();
  bool IsValidWord(const std::string& word) const;
  std::vector<std::string> FormedWordsFromPlay(const WordTileActionStruct& a) const;
  int ScorePlay(const WordTileActionStruct& a) const;
  void DealInitialRacks();
  void DrawTilesForPlayer(Player p, int count);
  bool RackHasTiles(Player p, const std::string& needed) const;
  void RemoveTilesFromRack(Player p, const std::string& needed);
  void AddTilesToRack(Player p, const std::string& tiles);
  void AddTilesToBag(const std::string& tiles);

  std::shared_ptr<const WordTileGame> game_;
  int board_size_ = kDefaultBoardSize;
  int rack_size_ = kDefaultRackSize;

  // Board representation: 15x15 grid of char ' ' empty or 'A'-'Z' or '?' for blank played (we store assigned letter separately maybe lowercase to indicate blank? Simplification: store uppercase letter even for blank, track blank positions separately in last play)
  std::vector<std::vector<char>> board_; // [row][col]
  std::vector<std::vector<Premium>> premiums_; // static layout

  // Racks: vector<string> size 2, each string of letters A-Z and '?' for blank, unsorted maybe sorted for canonical
  std::array<std::string, 2> racks_;
  // Bag counts index 0-25 A-Z, 26 blank
  std::array<int, kNumTileTypes> bag_counts_;
  std::array<double, 2> scores_{0.0, 0.0};
  Player current_player_ = 0;
  Phase phase_ = Phase::kDealInitial;
  int consecutive_scoreless_turns_ = 0;
  // Last play pending challenge storage
  bool pending_play_ = false;
  WordTileActionStruct pending_action_;
  Player pending_player_ = kInvalidPlayer;
  int pending_score_ = 0;
  // For undo simplistic we may store full state snapshot history via Clone mechanism already in base State history, so Undo can rely on full state restore via external mechanism? We'll implement simplified Undo that errors for now and rely on Clone for tree search.
};

class WordTileGame : public Game {
 public:
  explicit WordTileGame(const GameParameters& params);
  int NumDistinctActions() const override { return 1; } // dummy because action_structs_only=true
  std::unique_ptr<State> NewInitialState() const override;
  int NumPlayers() const override { return kNumPlayers; }
  double MinUtility() const override { return -1000; } // rough bounds based on max score ~ 2000?
  double MaxUtility() const override { return 1000; }
  absl::optional<double> UtilitySum() const override { return 0; }
  std::vector<int> ObservationTensorShape() const override;
  int MaxGameLength() const override { return kMaxGameLength; }
  std::string ActionToString(Player player, Action action_id) const override;

  // Game specific accessors
  const std::string& dictionary_path() const { return dictionary_path_; }
  int board_size() const { return board_size_; }
  int rack_size() const { return rack_size_; }
  bool IsValidWord(const std::string& word) const;

 private:
  void LoadDictionary();
  std::string dictionary_path_;
  int board_size_ = kDefaultBoardSize;
  int rack_size_ = kDefaultRackSize;
  absl::flat_hash_map<std::string, bool> dictionary_set_; // uppercase word -> true for O(1) lookup, trie later for generation
  // Tile distribution and values static tables defined in cc
};

}  // namespace word_tile
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_WORD_TILE_WORD_TILE_H_
