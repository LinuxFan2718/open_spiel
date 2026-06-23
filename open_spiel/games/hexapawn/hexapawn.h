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

#ifndef OPEN_SPIEL_GAMES_HEXAPAWN_H_
#define OPEN_SPIEL_GAMES_HEXAPAWN_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/types/optional.h"
#include "open_spiel/abseil-cpp/absl/types/span.h"
#include "open_spiel/json/include/nlohmann/json.hpp"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_globals.h"
#include "open_spiel/spiel_utils.h"

// Hexapawn: https://en.wikipedia.org/wiki/Hexapawn
// 3x3 board, 3 pawns per side. White moves up, Black moves down.
// Pawns move forward one to empty square, or capture diagonally forward.
// Win by reaching opposite back rank, capturing all opponent pawns,
// or stalemating opponent (no legal moves).

namespace open_spiel {
namespace hexapawn {

inline constexpr int kNumPlayers = 2;
inline constexpr int kNumRows = 3;
inline constexpr int kNumCols = 3;
inline constexpr int kNumCells = kNumRows * kNumCols;
inline constexpr int kCellStates = 1 + kNumPlayers;  // empty, white, black

enum class CellState {
  kEmpty,
  kWhite,  // Player 0
  kBlack,  // Player 1
};

struct HexapawnStructContents {
  std::string current_player;
  std::vector<std::string> board;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(HexapawnStructContents, current_player, board);
};

SPIEL_DEFINE_STRUCT(HexapawnStateStruct, StateStruct, HexapawnStructContents);
SPIEL_DEFINE_STRUCT(HexapawnObservationStruct, ObservationStruct,
                    HexapawnStructContents);

struct HexapawnActionStruct : public ActionStruct {
  int from_row;
  int from_col;
  int to_row;
  int to_col;
  SPIEL_STRUCT_BOILERPLATE(HexapawnActionStruct, from_row, from_col, to_row,
                           to_col);
};

class HexapawnState : public State {
 public:
  HexapawnState(std::shared_ptr<const Game> game);
  HexapawnState(std::shared_ptr<const Game> game,
                const HexapawnStateStruct& state_struct);

  HexapawnState(const HexapawnState&) = default;
  HexapawnState& operator=(const HexapawnState&) = default;

  Player CurrentPlayer() const override {
    return IsTerminal() ? kTerminalPlayerId : current_player_;
  }
  std::string ActionToString(Player player, Action action_id) const override;
  std::string ToString() const override;
  bool IsTerminal() const override;
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  void UndoAction(Player player, Action move) override;
  std::vector<Action> LegalActions() const override;
  std::vector<CellState> Board() const;
  CellState BoardAt(int cell) const { return board_[cell]; }
  CellState BoardAt(int row, int column) const {
    return board_[row * kNumCols + column];
  }
  Player outcome() const { return outcome_; }

  std::unique_ptr<StateStruct> ToStruct() const override;
  std::unique_ptr<ObservationStruct> ToObservationStruct(
      Player player) const override;
  std::unique_ptr<ActionStruct> ActionToStruct(
      Player player, Action action_id) const override;
  std::vector<Action> StructToActions(
      const ActionStruct& action_struct) const override;

 protected:
  void DoApplyAction(Action move) override;

 private:
  bool InBounds(int r, int c) const { return r >= 0 && r < kNumRows && c >= 0 && c < kNumCols; }
  bool HasNoPawns(Player player) const;
  int CountPawns(Player player) const;
  std::array<CellState, kNumCells> board_;
  Player current_player_ = 0;  // 0 = White, moves up
  Player outcome_ = kInvalidPlayer;
  int num_moves_ = 0;
};

class HexapawnGame : public Game {
 public:
  explicit HexapawnGame(const GameParameters& params);
  int NumDistinctActions() const override { return kNumCells * kNumCells; }
  std::unique_ptr<State> NewInitialState() const override {
    return std::unique_ptr<State>(new HexapawnState(shared_from_this()));
  }
  std::unique_ptr<State> NewInitialState(
      const HexapawnStateStruct& state_struct) const {
    return std::unique_ptr<State>(
        new HexapawnState(shared_from_this(), state_struct));
  }
  std::unique_ptr<State> NewInitialState(
      const nlohmann::json& json) const override {
    return NewInitialState(HexapawnStateStruct(json));
  }
  int NumPlayers() const override { return kNumPlayers; }
  double MinUtility() const override { return -1; }
  absl::optional<double> UtilitySum() const override { return 0; }
  double MaxUtility() const override { return 1; }
  std::vector<int> ObservationTensorShape() const override {
    return {kCellStates, kNumRows, kNumCols};
  }
  int MaxGameLength() const override { return 50; }  // generous upper bound
  std::string ActionToString(Player player, Action action_id) const override;
};

CellState PlayerToState(Player player);
std::string StateToString(CellState state);
std::string PlayerToString(Player player);

inline std::ostream& operator<<(std::ostream& stream, const CellState& state) {
  return stream << StateToString(state);
}

}  // namespace hexapawn
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HEXAPAWN_H_
