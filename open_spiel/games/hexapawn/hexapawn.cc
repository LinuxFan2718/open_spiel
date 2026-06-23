// Copyright 2026 DeepMind Technologies Limited
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

#include "open_spiel/games/hexapawn/hexapawn.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/abseil-cpp/absl/strings/str_format.h"
#include "open_spiel/abseil-cpp/absl/types/span.h"
#include "open_spiel/json/include/nlohmann/json.hpp"
#include "open_spiel/game_parameters.h"
#include "open_spiel/observer.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_globals.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/utils/tensor_view.h"

namespace open_spiel {
namespace hexapawn {
namespace {

const GameType kGameType{
    /*short_name=*/"hexapawn",
    /*long_name=*/"Hexapawn",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kDeterministic,
    GameType::Information::kPerfectInformation,
    GameType::Utility::kZeroSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/2,
    /*min_num_players=*/2,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/false,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/{}  // 3x3 only
};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::shared_ptr<const Game>(new HexapawnGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

RegisterSingleTensorObserver single_tensor(kGameType.short_name);

inline int EncodeAction(int from_row, int from_col, int to_row, int to_col) {
  int from = from_row * kNumCols + from_col;
  int to = to_row * kNumCols + to_col;
  return from * kNumCells + to;
}

inline void DecodeAction(Action action, int* from_row, int* from_col,
                         int* to_row, int* to_col) {
  int from = action / kNumCells;
  int to = action % kNumCells;
  *from_row = from / kNumCols;
  *from_col = from % kNumCols;
  *to_row = to / kNumCols;
  *to_col = to % kNumCols;
}

inline char ColToChar(int col) { return static_cast<char>('a' + col); }
inline char RowToChar(int row) {
  // row 0 top -> '3', row 2 bottom -> '1'
  return static_cast<char>('1' + (kNumRows - 1 - row));
}
inline std::string CoordToString(int row, int col) {
  std::string s;
  s.push_back(ColToChar(col));
  s.push_back(RowToChar(row));
  return s;
}

}  // namespace

CellState PlayerToState(Player player) {
  switch (player) {
    case 0:
      return CellState::kWhite;
    case 1:
      return CellState::kBlack;
    default:
      SpielFatalError(absl::StrCat("Invalid player id ", player));
      return CellState::kEmpty;
  }
}

std::string PlayerToString(Player player) {
  switch (player) {
    case 0:
      return "w";
    case 1:
      return "b";
    default:
      return DefaultPlayerString(player);
  }
}

std::string StateToString(CellState state) {
  switch (state) {
    case CellState::kEmpty:
      return ".";
    case CellState::kWhite:
      return "w";
    case CellState::kBlack:
      return "b";
    default:
      SpielFatalError("Unknown state.");
  }
}

CellState StringToCellState(const std::string& s) {
  if (s == "w") return CellState::kWhite;
  if (s == "b") return CellState::kBlack;
  if (s == ".") return CellState::kEmpty;
  SpielFatalError(absl::StrCat("Invalid cell string: ", s));
}

std::vector<CellState> HexapawnState::Board() const {
  return std::vector<CellState>(board_.begin(), board_.end());
}

HexapawnState::HexapawnState(std::shared_ptr<const Game> game) : State(game) {
  std::fill(board_.begin(), board_.end(), CellState::kEmpty);
  // Black pawns on top row (row 0), White pawns on bottom row (row 2)
  for (int c = 0; c < kNumCols; ++c) {
    board_[0 * kNumCols + c] = CellState::kBlack;
    board_[2 * kNumCols + c] = CellState::kWhite;
  }
  current_player_ = 0;  // White first
  outcome_ = kInvalidPlayer;
  num_moves_ = 0;
}

int HexapawnState::CountPawns(Player player) const {
  CellState target = PlayerToState(player);
  int count = 0;
  for (auto cs : board_) if (cs == target) ++count;
  return count;
}

bool HexapawnState::HasNoPawns(Player player) const {
  return CountPawns(player) == 0;
}

std::vector<Action> HexapawnState::LegalActions() const {
  if (IsTerminal()) return {};
  std::vector<Action> moves;
  Player player = current_player_;
  CellState my_state = PlayerToState(player);
  CellState opp_state = PlayerToState(1 - player);
  int dir = (player == 0) ? -1 : +1;  // White up, Black down

  for (int r = 0; r < kNumRows; ++r) {
    for (int c = 0; c < kNumCols; ++c) {
      if (BoardAt(r, c) != my_state) continue;
      int nr = r + dir;
      if (!InBounds(nr, c)) continue;
      // Forward move to empty
      if (BoardAt(nr, c) == CellState::kEmpty) {
        moves.push_back(EncodeAction(r, c, nr, c));
      }
      // Diagonal captures
      for (int dc : {-1, 1}) {
        int nc = c + dc;
        if (InBounds(nr, nc) && BoardAt(nr, nc) == opp_state) {
          moves.push_back(EncodeAction(r, c, nr, nc));
        }
      }
    }
  }
  std::sort(moves.begin(), moves.end());
  return moves;
}

void HexapawnState::DoApplyAction(Action move) {
  int fr, fc, tr, tc;
  DecodeAction(move, &fr, &fc, &tr, &tc);
  SPIEL_CHECK_TRUE(InBounds(fr, fc));
  SPIEL_CHECK_TRUE(InBounds(tr, tc));
  CellState my_state = PlayerToState(current_player_);
  CellState opp_state = PlayerToState(1 - current_player_);
  SPIEL_CHECK_EQ(board_[fr * kNumCols + fc], my_state);
  SPIEL_CHECK_TRUE(board_[tr * kNumCols + tc] == CellState::kEmpty ||
                   board_[tr * kNumCols + tc] == opp_state);

  // Move piece
  board_[tr * kNumCols + tc] = my_state;
  board_[fr * kNumCols + fc] = CellState::kEmpty;

  // Check win by reaching back rank
  if ((current_player_ == 0 && tr == 0) ||
      (current_player_ == 1 && tr == kNumRows - 1)) {
    outcome_ = current_player_;
  } else if (HasNoPawns(1 - current_player_)) {
    // Captured all opponent pawns
    outcome_ = current_player_;
  }

  // Switch player
  current_player_ = 1 - current_player_;
  num_moves_ += 1;

  // Check stalemate: if not already terminal, and next player has no legal moves,
  // then current player (who just moved) wins.
  if (outcome_ == kInvalidPlayer) {
    // Temporarily check legal actions for next player
    // LegalActions uses current_player_, so we can call it directly.
    if (LegalActions().empty()) {
      outcome_ = 1 - current_player_;  // previous player wins
    }
  }
}

bool HexapawnState::IsTerminal() const {
  return outcome_ != kInvalidPlayer;
}

std::vector<double> HexapawnState::Returns() const {
  if (outcome_ == 0) {
    return {1.0, -1.0};
  } else if (outcome_ == 1) {
    return {-1.0, 1.0};
  } else {
    return {0.0, 0.0};
  }
}

std::string HexapawnState::ToString() const {
  std::string str;
  for (int r = 0; r < kNumRows; ++r) {
    for (int c = 0; c < kNumCols; ++c) {
      absl::StrAppend(&str, StateToString(BoardAt(r, c)));
    }
    if (r < kNumRows - 1) absl::StrAppend(&str, "\n");
  }
  return str;
}

std::string HexapawnState::InformationStateString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return HistoryString();
}

std::string HexapawnState::ObservationString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return ToString();
}

void HexapawnState::ObservationTensor(Player player,
                                      absl::Span<float> values) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  TensorView<2> view(values, {kCellStates, kNumCells}, true);
  for (int cell = 0; cell < kNumCells; ++cell) {
    view[{static_cast<int>(board_[cell]), cell}] = 1.0;
  }
}

std::unique_ptr<State> HexapawnState::Clone() const {
  return std::unique_ptr<State>(new HexapawnState(*this));
}

void HexapawnState::UndoAction(Player player, Action move) {
  // Simplified undo: not fully reversible without history of captured piece.
  // For Hexapawn we can recompute from history by cloning from start is typical,
  // but we implement approximate reverse assuming we know move structure.
  // This is complex because we need to know if capture occurred.
  // Instead, we rely on State's history to not require perfect undo for MCTS?
  // OpenSpiel expects Undo to work. We'll implement by decoding and reversing,
  // restoring captured pawn based on board state before move is lost.
  // To properly support undo, we need to store last captured info, but we can
  // approximate by assuming board before move had empty source and piece at destination moved back.
  // However we lose captured piece type. We can infer: if destination after move
  // contains moving player's piece, source is empty, and we know moving player.
  // Captured piece must have been opponent. So we can restore opponent pawn
  // if the move was diagonal (capture), else empty.
  int fr, fc, tr, tc;
  DecodeAction(move, &fr, &fc, &tr, &tc);
  CellState mover = PlayerToState(player);
  CellState opponent = PlayerToState(1 - player);
  // Move piece back
  board_[fr * kNumCols + fc] = mover;
  // Determine if capture: diagonal move implies capture in Hexapawn rules
  // (forward moves are vertical only). Actually forward is same column,
  // diagonal is capture.
  if (fc == tc) {
    board_[tr * kNumCols + tc] = CellState::kEmpty;
  } else {
    board_[tr * kNumCols + tc] = opponent;
  }
  current_player_ = player;
  outcome_ = kInvalidPlayer;
  num_moves_ -= 1;
  history_.pop_back();
  --move_number_;
}

std::string HexapawnState::ActionToString(Player player, Action action_id) const {
  return game_->ActionToString(player, action_id);
}

std::string HexapawnGame::ActionToString(Player player, Action action_id) const {
  int fr, fc, tr, tc;
  DecodeAction(action_id, &fr, &fc, &tr, &tc);
  std::string from = CoordToString(fr, fc);
  std::string to = CoordToString(tr, tc);
  std::string sep = (fc == tc) ? "-" : "x";
  return absl::StrCat(from, sep, to);
}

std::unique_ptr<StateStruct> HexapawnState::ToStruct() const {
  auto rv = std::make_unique<HexapawnStateStruct>();
  std::vector<std::string> board;
  board.reserve(board_.size());
  for (const CellState& cell : board_) {
    board.push_back(StateToString(cell));
  }
  rv->current_player = PlayerToString(CurrentPlayer());
  rv->board = board;
  return rv;
}

std::unique_ptr<ObservationStruct> HexapawnState::ToObservationStruct(
    Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return std::make_unique<HexapawnObservationStruct>(this->ToJson());
}

std::unique_ptr<ActionStruct> HexapawnState::ActionToStruct(
    Player player, Action action_id) const {
  int fr, fc, tr, tc;
  DecodeAction(action_id, &fr, &fc, &tr, &tc);
  auto action_struct = std::make_unique<HexapawnActionStruct>();
  action_struct->from_row = fr;
  action_struct->from_col = fc;
  action_struct->to_row = tr;
  action_struct->to_col = tc;
  return action_struct;
}

std::vector<Action> HexapawnState::StructToActions(
    const ActionStruct& action_struct) const {
  const auto* a = SafeActionCast<HexapawnActionStruct>(action_struct);
  SPIEL_CHECK_GE(a->from_row, 0);
  SPIEL_CHECK_LT(a->from_row, kNumRows);
  SPIEL_CHECK_GE(a->from_col, 0);
  SPIEL_CHECK_LT(a->from_col, kNumCols);
  SPIEL_CHECK_GE(a->to_row, 0);
  SPIEL_CHECK_LT(a->to_row, kNumRows);
  SPIEL_CHECK_GE(a->to_col, 0);
  SPIEL_CHECK_LT(a->to_col, kNumCols);
  return {EncodeAction(a->from_row, a->from_col, a->to_row, a->to_col)};
}

HexapawnState::HexapawnState(std::shared_ptr<const Game> game,
                             const HexapawnStateStruct& state_struct)
    : State(game) {
  std::fill(board_.begin(), board_.end(), CellState::kEmpty);
  if (state_struct.board.size() != kNumCells) {
    SpielFatalError(absl::StrFormat("Invalid board size: expected %d, got %d",
                                    kNumCells, state_struct.board.size()));
  }
  num_moves_ = 0;
  int num_w = 0;
  int num_b = 0;
  for (int i = 0; i < kNumCells; ++i) {
    CellState cs = StringToCellState(state_struct.board[i]);
    board_[i] = cs;
    if (cs == CellState::kWhite) num_w++;
    if (cs == CellState::kBlack) num_b++;
  }
  // Determine current player from string, fallback to counts
  if (state_struct.current_player == "w") {
    current_player_ = 0;
  } else if (state_struct.current_player == "b") {
    current_player_ = 1;
  } else {
    SpielFatalError("Invalid current player string");
  }

  // Determine outcome: check back rank win or no pawns or stalemate
  outcome_ = kInvalidPlayer;
  // Back rank check
  for (int c = 0; c < kNumCols; ++c) {
    if (board_[0 * kNumCols + c] == CellState::kWhite) outcome_ = 0;
    if (board_[2 * kNumCols + c] == CellState::kBlack) outcome_ = 1;
  }
  if (num_w == 0) outcome_ = 1;
  if (num_b == 0) outcome_ = 0;
  // Stalemate check if not already terminal
  if (outcome_ == kInvalidPlayer) {
    HexapawnState temp(*this);
    if (temp.LegalActions().empty()) {
      outcome_ = 1 - current_player_;
    }
  }
  starting_state_str_ = this->ToJson();
}

HexapawnGame::HexapawnGame(const GameParameters& params)
    : Game(kGameType, params) {}

}  // namespace hexapawn
}  // namespace open_spiel
