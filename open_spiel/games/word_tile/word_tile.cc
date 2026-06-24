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

#include "open_spiel/games/word_tile/word_tile.h"

#include "open_spiel/games/word_tile/board.h"
#include "open_spiel/games/word_tile/dictionary.h"
#include "open_spiel/games/word_tile/tiles.h"

#include <algorithm>
#include <fstream>
#include <random>

#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/utils/status.h"

namespace open_spiel {
namespace word_tile {

namespace {

const GameType kGameType{
    /*short_name=*/"word_tile",
    /*long_name=*/"Word Tile Game",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kExplicitStochastic,
    GameType::Information::kImperfectInformation,
    GameType::Utility::kZeroSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/2,
    /*min_num_players=*/2,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/false,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/
    {{"dictionary_file", GameParameter(std::string(""))},
     {"board_size", GameParameter(kDefaultBoardSize)},
     {"rack_size", GameParameter(kDefaultRackSize)}},
    /*default_loadable=*/true,
    /*provides_factored_observation_string=*/false,
    /*action_structs_only=*/true
};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::shared_ptr<const Game>(new WordTileGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

// Helper: parse blank_assignments string "2:J,5:S" -> map pos->char
absl::flat_hash_map<int, char> ParseBlankAssignments(const std::string& s) {
  absl::flat_hash_map<int, char> out;
  if (s.empty()) return out;
  size_t start = 0;
  while (start < s.size()) {
    size_t colon = s.find(':', start);
    if (colon == std::string::npos) break;
    size_t comma = s.find(',', colon);
    std::string pos_str = s.substr(start, colon - start);
    std::string char_str = s.substr(colon + 1, (comma == std::string::npos ? s.size() : comma) - colon - 1);
    int pos = std::atoi(pos_str.c_str());
    char c = char_str.empty() ? '?' : std::toupper(char_str[0]);
    out[pos] = c;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

// Check if board is empty (first move)
bool BoardIsEmpty(const std::vector<std::vector<char>>& board) {
  for (const auto& row : board) {
    for (char c : row) {
      if (c != ' ') return false;
    }
  }
  return true;
}

}  // namespace

// ---- WordTileGame ----
WordTileGame::WordTileGame(const GameParameters& params)
    : Game(kGameType, params),
      dictionary_path_(::open_spiel::ParameterValue<std::string>(params, "dictionary_file", std::string(""))),
      board_size_(::open_spiel::ParameterValue<int>(params, "board_size", kDefaultBoardSize)),
      rack_size_(::open_spiel::ParameterValue<int>(params, "rack_size", kDefaultRackSize)) {
  if (dictionary_path_.empty()) {
    // Try default locations relative to runfiles / source tree
    const std::vector<std::string> candidates = {
        "open_spiel/games/word_tile/data/enable.txt",
        "../open_spiel/games/word_tile/data/enable.txt",
        "games/word_tile/data/enable.txt",
        "/home/denniscahillane/open_spiel/open_spiel/games/word_tile/data/enable.txt"
    };
    for (const auto& p : candidates) {
      std::ifstream fin(p);
      if (fin.good()) { dictionary_path_ = p; break; }
    }
  }
  LoadDictionary();
}

void WordTileGame::LoadDictionary() {
  // Lazy loading on first use to avoid file not found during registration.
  // We'll load on demand in IsValidWord.
}

bool WordTileGame::IsValidWord(const std::string& word) const {
  static absl::flat_hash_map<std::string, std::unique_ptr<Dictionary>> cache;
  auto it = cache.find(dictionary_path_);
  if (it == cache.end()) {
    try {
      auto dict = std::make_unique<Dictionary>(dictionary_path_);
      bool result = dict->IsValidWord(word);
      cache[dictionary_path_] = std::move(dict);
      return result;
    } catch (...) {
      // If file missing, allow all words for stub phase
      return true;
    }
  }
  return it->second->IsValidWord(word);
}

std::unique_ptr<State> WordTileGame::NewInitialState() const {
  return std::unique_ptr<State>(new WordTileState(shared_from_this()));
}

std::vector<int> WordTileGame::ObservationTensorShape() const {
  // 33 planes as discussed: empty, A-Z, blank, DL, TL, DW, TW, star, last move = let's approximate 33
  return {33, board_size_, board_size_};
}

std::string WordTileGame::ActionToString(Player player, Action action_id) const {
  return "action_struct_only";
}

// ---- WordTileState ----
WordTileState::WordTileState(std::shared_ptr<const Game> game)
    : State(game), game_(std::dynamic_pointer_cast<const WordTileGame>(game)) {
  board_size_ = game_->board_size();
  rack_size_ = game_->rack_size();
  board_.assign(board_size_, std::vector<char>(board_size_, ' '));
  premiums_ = CreateStandardBoard();
  InitializeTileBag();
  scores_ = {0.0, 0.0};
  current_player_ = kChancePlayerId;
  phase_ = Phase::kDealInitial;
  racks_[0] = "";
  racks_[1] = "";
  initial_deal_count_ = 0;
  draw_player_ = kInvalidPlayer;
  tiles_to_draw_ = 0;
}

void WordTileState::InitializeBoardPremiums() {
  premiums_ = CreateStandardBoard();
}

void WordTileState::InitializeTileBag() {
  for (int i = 0; i < kNumTileTypes; ++i) {
    bag_counts_[i] = kTileDistribution[i];
  }
}

Player WordTileState::CurrentPlayer() const {
  if (IsTerminal()) return kTerminalPlayerId;
  if (phase_ == Phase::kDealInitial || phase_ == Phase::kDraw) return kChancePlayerId;
  if (phase_ == Phase::kChallenge) return 1 - pending_player_; // opponent decides
  return current_player_;
}

bool WordTileState::IsChanceNode() const {
  return phase_ == Phase::kDealInitial || phase_ == Phase::kDraw;
}

std::vector<std::pair<Action, double>> WordTileState::ChanceOutcomes() const {
  SPIEL_CHECK_TRUE(IsChanceNode());
  int total = 0;
  for (int c : bag_counts_) total += c;
  if (total == 0) {
    return {{0, 1.0}};  // No tiles left, dummy outcome
  }
  std::vector<std::pair<Action, double>> outcomes;
  for (int tile = 0; tile < kNumTileTypes; ++tile) {
    int count = bag_counts_[tile];
    if (count > 0) {
      outcomes.push_back({tile, static_cast<double>(count) / total});
    }
  }
  return outcomes;
}

std::vector<Action> WordTileState::LegalActions() const {
  // Action structs only game returns empty vector; actual legal actions via action struct interface not yet implemented.
  // For stub, return empty to indicate use action structs.
  return {};
}

std::string WordTileState::ActionToString(Player player, Action action) const {
  if (IsChanceNode() || player == kChancePlayerId) {
    if (action >= 0 && action < kNumTileTypes) {
      char c = IndexToLetter(action);
      return std::string("draw:") + c;
    }
    return absl::StrCat("chance:", action);
  }
  return "action_struct_only";
}

std::string WordTileState::ToString() const {
  std::string s;
  s += "Scores: P0=" + std::to_string((int)scores_[0]) + " P1=" + std::to_string((int)scores_[1]) + "\n";
  if (IsTerminal()) {
    s += "Game over\n";
  } else if (IsChanceNode()) {
    s += "Chance node (drawing tiles)\n";
  } else {
    const char* phase_names[] = {"DealInitial", "Play", "Challenge", "Draw", "GameOver"};
    int phase_idx = static_cast<int>(phase_);
    std::string phase_str = (phase_idx >= 0 && phase_idx < 5) ? phase_names[phase_idx] : "Unknown";
    s += "Player " + std::to_string(current_player_) + " to act, phase: " + phase_str + "\n";
  }
  s += "Rack0: " + racks_[0] + " Rack1: " + racks_[1] + "\n";
  int bag_total = 0;
  for (int c : bag_counts_) bag_total += c;
  s += "Bag tiles remaining: " + std::to_string(bag_total) + "\n";
  s += "\n  ";
  for (int c = 0; c < board_size_; ++c) {
    s += std::string(1, 'A' + (c % 26));
    s += " ";
  }
  s += "\n";
  for (int r = 0; r < board_size_; ++r) {
    char row_label = 'A' + (r % 26);
    s += row_label;
    s += " ";
    for (int c = 0; c < board_size_; ++c) {
      char ch = board_[r][c];
      if (ch == ' ') {
        // Show premium squares for empty cells
        Premium p = premiums_[r][c];
        char prem_char = '.';
        switch (p) {
          case Premium::kDoubleLetter: prem_char = 'd'; break;
          case Premium::kTripleLetter: prem_char = 't'; break;
          case Premium::kDoubleWord: prem_char = 'D'; break;
          case Premium::kTripleWord: prem_char = 'T'; break;
          default: prem_char = '.'; break;
        }
        s += prem_char;
      } else {
        s += ch;
      }
      s += " ";
    }
    s += "\n";
  }
  if (pending_play_) {
    s += "Pending play by P" + std::to_string(pending_player_) + " score " + std::to_string(pending_score_) + " awaiting challenge\n";
  }
  return s;
}

bool WordTileState::IsTerminal() const { return phase_ == Phase::kGameOver; }

std::vector<double> WordTileState::Returns() const {
  if (!IsTerminal()) return {0.0,0.0};
  double diff = scores_[0] - scores_[1];
  if (diff > 0) return {1.0, -1.0};
  if (diff < 0) return {-1.0, 1.0};
  return {0.0,0.0};
}

std::string WordTileState::InformationStateString(Player player) const {
  // Hide opponent rack
  std::string s = "P" + std::to_string(player) + " info\n";
  s += "My rack: " + racks_[player] + "\n";
  s += "Opp rack size: " + std::to_string(racks_[1-player].size()) + "\n";
  s += "Scores " + std::to_string((int)scores_[0]) + " " + std::to_string((int)scores_[1]) + "\n";
  int total=0; for(int c: bag_counts_) total+=c;
  s += "Bag total: " + std::to_string(total) + "\n";
  if (IsTerminal()) {
    s += "Game over\n";
  } else if (IsChanceNode()) {
    s += "Chance node (drawing tiles)\n";
  } else {
    const char* phase_names[] = {"DealInitial", "Play", "Challenge", "Draw", "GameOver"};
    int phase_idx = static_cast<int>(phase_);
    std::string phase_str = (phase_idx >= 0 && phase_idx < 5) ? phase_names[phase_idx] : "Unknown";
    s += "Player " + std::to_string(current_player_) + " to act, phase: " + phase_str + "\n";
  }
  if (pending_play_) {
    s += "Pending play by P" + std::to_string(pending_player_) + " score " + std::to_string(pending_score_) + " awaiting challenge\n";
  }
  s += "\n  ";
  for (int c = 0; c < board_size_; ++c) {
    s += std::string(1, 'A' + (c % 26));
    s += " ";
  }
  s += "\n";
  for (int r = 0; r < board_size_; ++r) {
    char row_label = 'A' + (r % 26);
    s += row_label;
    s += " ";
    for (int c = 0; c < board_size_; ++c) {
      char ch = board_[r][c];
      if (ch == ' ') {
        Premium p = premiums_[r][c];
        char prem_char = '.';
        switch (p) {
          case Premium::kDoubleLetter: prem_char = 'd'; break;
          case Premium::kTripleLetter: prem_char = 't'; break;
          case Premium::kDoubleWord: prem_char = 'D'; break;
          case Premium::kTripleWord: prem_char = 'T'; break;
          default: prem_char = '.'; break;
        }
        s += prem_char;
      } else {
        s += ch;
      }
      s += " ";
    }
    s += "\n";
  }
  return s;
}

std::string WordTileState::ObservationString(Player player) const {
  return InformationStateString(player);
}

void WordTileState::ObservationTensor(Player player, absl::Span<float> values) const {
  // Observation tensor shape: {33, board_size, board_size}
  // Planes:
  //  0: empty squares
  //  1-26: A-Z letters on board
  //  27: blank tiles on board
  //  28: Double Letter premium
  //  29: Triple Letter premium
  //  30: Double Word premium
  //  31: Triple Word premium
  //  32: unused / reserved
  int num_planes = 33;
  int plane_size = board_size_ * board_size_;
  SPIEL_CHECK_EQ(values.size(), num_planes * plane_size);
  std::fill(values.begin(), values.end(), 0.0f);
  auto idx = [&](int plane, int r, int c) {
    return plane * plane_size + r * board_size_ + c;
  };
  for (int r = 0; r < board_size_; ++r) {
    for (int c = 0; c < board_size_; ++c) {
      char board_char = board_[r][c];
      if (board_char == ' ') {
        values[idx(0, r, c)] = 1.0f;
      } else {
        int key = r * board_size_ + c;
        bool is_blank = blank_assignments_board_.find(key) != blank_assignments_board_.end();
        if (is_blank) {
          values[idx(27, r, c)] = 1.0f;
        } else {
          int letter_idx = LetterToIndex(board_char);
          if (letter_idx >= 0 && letter_idx < 26) {
            values[idx(1 + letter_idx, r, c)] = 1.0f;
          }
        }
      }
      // Premium planes (static board features)
      Premium p = premiums_[r][c];
      switch (p) {
        case Premium::kDoubleLetter: values[idx(28, r, c)] = 1.0f; break;
        case Premium::kTripleLetter: values[idx(29, r, c)] = 1.0f; break;
        case Premium::kDoubleWord: values[idx(30, r, c)] = 1.0f; break;
        case Premium::kTripleWord: values[idx(31, r, c)] = 1.0f; break;
        default: break;
      }
    }
  }
  // Plane 32 left as zeros (could be used for turn indicator, etc.)
}

std::unique_ptr<State> WordTileState::Clone() const {
  return std::unique_ptr<State>(new WordTileState(*this));
}

void WordTileState::UndoAction(Player player, Action action) {
  SpielFatalError("Undo not implemented for stub");
}

void WordTileState::DoApplyAction(Action action) {
  SPIEL_CHECK_TRUE(IsChanceNode());
  int tile_idx = static_cast<int>(action);
  SPIEL_CHECK_GE(tile_idx, 0);
  SPIEL_CHECK_LT(tile_idx, kNumTileTypes);
  SPIEL_CHECK_GT(bag_counts_[tile_idx], 0);
  char tile_char = IndexToLetter(tile_idx);
  bag_counts_[tile_idx]--;

  if (phase_ == Phase::kDealInitial) {
    Player deal_player = (initial_deal_count_ < rack_size_) ? 0 : 1;
    racks_[deal_player].push_back(tile_char);
    initial_deal_count_++;
    if (initial_deal_count_ >= 2 * rack_size_) {
      // Initial deal complete
      phase_ = Phase::kPlay;
      current_player_ = 0;
    }
    // else stay in DealInitial chance node
    return;
  } else if (phase_ == Phase::kDraw) {
    SPIEL_CHECK_NE(draw_player_, kInvalidPlayer);
    racks_[draw_player_].push_back(tile_char);
    tiles_to_draw_--;
    if (tiles_to_draw_ <= 0) {
      // Drawing complete, switch turn
      phase_ = Phase::kPlay;
      current_player_ = 1 - draw_player_;
      draw_player_ = kInvalidPlayer;
    }
    // else stay in Draw chance node to draw next tile
    return;
  }
  SpielFatalError("DoApplyAction chance in invalid phase");
}

void WordTileState::DoApplyAction(const WordTileActionStruct& a) {
  if (a.type == "pass") {
    consecutive_scoreless_turns_++;
    if (consecutive_scoreless_turns_ >= 6) {
      FinalizeScores();
      phase_ = Phase::kGameOver;
      current_player_ = kTerminalPlayerId;
    } else {
      current_player_ = 1 - current_player_;
      phase_ = Phase::kPlay;
    }
    return;
  }
  if (a.type == "exchange") {
    // Validate exchange_tiles length
    SPIEL_CHECK_TRUE(RackHasTiles(current_player_, a.exchange_tiles));
    // Remove tiles from rack, add to bag
    RemoveTilesFromRack(current_player_, a.exchange_tiles);
    AddTilesToBag(a.exchange_tiles);
    consecutive_scoreless_turns_++;
    if (consecutive_scoreless_turns_ >= 6) {
      FinalizeScores();
      phase_ = Phase::kGameOver;
      current_player_ = kTerminalPlayerId;
      return;
    }
    // Draw replacement tiles
    int to_draw = a.exchange_tiles.size();
    // Check if bag has enough tiles to exchange (need at least 7 tiles in bag per standard rules)
    int bag_total = 0;
    for (int c : bag_counts_) bag_total += c;
    if (bag_total < 7) {
      // Not allowed to exchange normally, but we already validated? For now allow.
    }
    draw_player_ = current_player_;
    tiles_to_draw_ = to_draw;
    if (tiles_to_draw_ > 0 && bag_total > 0) {
      phase_ = Phase::kDraw;
      current_player_ = kChancePlayerId;
    } else {
      // No tiles to draw, go to next player
      phase_ = Phase::kPlay;
      current_player_ = 1 - current_player_;
    }
    return;
  }
  if (a.type == "accept") {
    SPIEL_CHECK_TRUE(pending_play_);
    SPIEL_CHECK_TRUE(phase_ == Phase::kChallenge);
    // Commit the pending play (already applied to board, score already added)
    Player play_player = pending_player_;
    int tiles_played_count = 0;
    for (char c : pending_action_.tiles_played) if (c != '.') tiles_played_count++;
    // Check end game: if rack empty and bag empty
    int bag_total = 0;
    for (int bc : bag_counts_) bag_total += bc;
    bool rack_empty = racks_[play_player].empty();
    if (rack_empty && bag_total == 0) {
      // Game over – finalize scores (subtract remaining racks, award opponent rack to finisher)
      pending_play_ = false;
      pending_action_ = WordTileActionStruct();
      pending_player_ = kInvalidPlayer;
      pending_score_ = 0;
      FinalizeScores();
      phase_ = Phase::kGameOver;
      current_player_ = kTerminalPlayerId;
      return;
    }
    // Otherwise, proceed to draw replacement tiles
    pending_play_ = false;
    draw_player_ = play_player;
    tiles_to_draw_ = tiles_played_count;
    if (tiles_to_draw_ > bag_total) tiles_to_draw_ = bag_total;
    pending_action_ = WordTileActionStruct();
    pending_player_ = kInvalidPlayer;
    pending_score_ = 0;
    consecutive_scoreless_turns_ = 0;
    if (tiles_to_draw_ > 0) {
      phase_ = Phase::kDraw;
      current_player_ = kChancePlayerId;
    } else {
      phase_ = Phase::kPlay;
      current_player_ = 1 - draw_player_;
      draw_player_ = kInvalidPlayer;
    }
    return;
  }
  if (a.type == "challenge") {
    SPIEL_CHECK_TRUE(pending_play_);
    SPIEL_CHECK_TRUE(phase_ == Phase::kChallenge);
    // Check if any formed word is invalid
    std::vector<std::string> formed = FormedWordsFromPlay(pending_action_);
    bool all_valid = true;
    for (const std::string& w : formed) {
      if (!IsValidWord(w)) { all_valid = false; break; }
    }
    Player challenger = 1 - pending_player_;
    if (all_valid) {
      // Challenge failed: challenger loses turn, play stands (already scored)
      Player play_player = pending_player_;
      int tiles_played_count = 0;
      for (char c : pending_action_.tiles_played) if (c != '.') tiles_played_count++;
      // Check end game: if rack empty and bag empty
      int bag_total = 0;
      for (int bc : bag_counts_) bag_total += bc;
      bool rack_empty = racks_[play_player].empty();
      // Note: rack still contains tiles that were just played? No, rack was already updated when play was applied,
      // so racks_[play_player] is post-play rack (before draw). So empty check is valid.
      // However, our pending_play_ path removed tiles from rack at place time, so yes rack is post-play.
      // Actually need to check: in DoApplyAction(place), we RemoveTilesFromRack, then score, then set pending_play_=true.
      // So yes, rack is already reduced.
      if (rack_empty && bag_total == 0) {
        pending_play_ = false;
        pending_action_ = WordTileActionStruct();
        pending_player_ = kInvalidPlayer;
        pending_score_ = 0;
        FinalizeScores();
        phase_ = Phase::kGameOver;
        current_player_ = kTerminalPlayerId;
        return;
      }
      // Proceed to draw phase
      pending_play_ = false;
      draw_player_ = play_player;
      tiles_to_draw_ = tiles_played_count;
      if (tiles_to_draw_ > bag_total) tiles_to_draw_ = bag_total;
      pending_action_ = WordTileActionStruct();
      pending_player_ = kInvalidPlayer;
      pending_score_ = 0;
      consecutive_scoreless_turns_ = 0;
      if (tiles_to_draw_ > 0) {
        phase_ = Phase::kDraw;
        current_player_ = kChancePlayerId;
      } else {
        phase_ = Phase::kPlay;
        current_player_ = 1 - draw_player_;
        draw_player_ = kInvalidPlayer;
      }
    } else {
      // Challenge successful: remove tiles from board, return tiles to rack, challenger gets turn
      // Undo the board placement
      int len = pending_action_.word.size();
      int dr = (pending_action_.direction == 1) ? 1 : 0;
      int dc = (pending_action_.direction == 0) ? 1 : 0;
      for (int i = 0; i < len; ++i) {
        char played = (i < (int)pending_action_.tiles_played.size()) ? pending_action_.tiles_played[i] : '.';
        if (played == '.') continue;
        int r = pending_action_.start_row + dr * i;
        int c = pending_action_.start_col + dc * i;
        board_[r][c] = ' ';
        int key = r * board_size_ + c;
        blank_assignments_board_.erase(key);
      }
      // Restore rack
      std::string tiles_used;
      for (char c : pending_action_.tiles_played) if (c != '.') tiles_used.push_back(c);
      AddTilesToRack(pending_player_, tiles_used);
      // Revert score
      scores_[pending_player_] -= pending_score_;
      pending_play_ = false;
      pending_action_ = WordTileActionStruct();
      pending_player_ = kInvalidPlayer;
      pending_score_ = 0;
      consecutive_scoreless_turns_++;
      // Challenger (current player) gets turn, stay in Play phase
      phase_ = Phase::kPlay;
      current_player_ = challenger;
      // Check for game end by scoreless turns?
      if (consecutive_scoreless_turns_ >= 6) {
        FinalizeScores();
        phase_ = Phase::kGameOver;
        current_player_ = kTerminalPlayerId;
      }
    }
    return;
  }
  if (a.type == "place") {
    // Validate
    std::string err = ValidatePlaceAction(a, current_player_);
    if (!err.empty()) {
      SpielFatalError(err);
    }
    // Remove tiles from rack
    RemoveTilesFromRack(current_player_, a.tiles_played);
    // Score play
    int score = ScorePlay(a);
    // Apply tiles to board
    int len = a.word.size();
    int dr = (a.direction == 1) ? 1 : 0;
    int dc = (a.direction == 0) ? 1 : 0;
    auto blank_map = ParseBlankAssignments(a.blank_assignments);
    for (int i = 0; i < len; ++i) {
      char played = (i < (int)a.tiles_played.size()) ? a.tiles_played[i] : '.';
      if (played == '.') continue;
      int r = a.start_row + dr * i;
      int c = a.start_col + dc * i;
      char word_char = std::toupper(a.word[i]);
      board_[r][c] = word_char;
      if (played == '?') {
        int key = r * board_size_ + c;
        blank_assignments_board_[key] = word_char;
      }
    }
    // Store pending play for challenge phase
    pending_play_ = true;
    pending_action_ = a;
    pending_player_ = current_player_;
    pending_score_ = score;
    scores_[current_player_] += score;
    phase_ = Phase::kChallenge;
    current_player_ = 1 - pending_player_;  // opponent decides challenge
    return;
  }
  SpielFatalError("Unknown action type: " + a.type);
}

std::unique_ptr<ActionStruct> WordTileState::ActionToStruct(Player player, Action action_id) const {
  // Flat actions are not used (action_structs_only=true).
  // Return a pass action as fallback to avoid crash if called.
  auto a = std::make_unique<WordTileActionStruct>();
  a->type = "pass";
  return a;
}
std::vector<Action> WordTileState::StructToActions(const ActionStruct& action_struct) const {
  return {0}; // dummy single flat action mapping for action-struct-only interface compliance
}
Status WordTileState::ApplyActionStruct(const ActionStruct& action_struct) {
  const auto* a = dynamic_cast<const WordTileActionStruct*>(&action_struct);
  if (!a) return ErrorStatus("Expected WordTileActionStruct");
  Status valid = ValidateActionStruct(action_struct);
  if (!valid.ok()) return valid;
  DoApplyAction(*a);
  return OkStatus();
}
Status WordTileState::ValidateActionStruct(const ActionStruct& action_struct) const {
  const auto* a = dynamic_cast<const WordTileActionStruct*>(&action_struct);
  if (!a) return ErrorStatus("Wrong type");
  Player player = CurrentPlayer();
  if (phase_ == Phase::kPlay) {
    if (a->type == "pass") return OkStatus();
    if (a->type == "exchange") {
      if (a->exchange_tiles.empty() || a->exchange_tiles.size() > (size_t)rack_size_) {
        return ErrorStatus("Invalid exchange_tiles size");
      }
      if (!RackHasTiles(player, a->exchange_tiles)) {
        return ErrorStatus("Rack does not have exchange tiles");
      }
      int bag_total = 0;
      for (int c : bag_counts_) bag_total += c;
      if (bag_total < 7) {
        return ErrorStatus("Not enough tiles in bag to exchange");
      }
      return OkStatus();
    }
    if (a->type == "place") {
      std::string err = ValidatePlaceAction(*a, player);
      if (!err.empty()) return ErrorStatus(err);
      return OkStatus();
    }
    return ErrorStatus("Invalid action type for Play phase");
  }
  if (phase_ == Phase::kChallenge) {
    if (a->type == "challenge" || a->type == "accept") return OkStatus();
    return ErrorStatus("Invalid action type for Challenge phase");
  }
  return ErrorStatus("Invalid action for phase");
}
std::unique_ptr<ActionStructSampler> WordTileState::GetActionStructSampler(int seed) const {
  // Random move generator: try to find a valid place action by
  // generating words from rack letters and trying to fit them on board.
  class RandomSampler : public ActionStructSampler {
   public:
    explicit RandomSampler(const State* s, int seed)
        : ActionStructSampler(s, seed), rng_(seed) {}
    std::unique_ptr<ActionStruct> SampleActionStruct() override {
      const WordTileState* state = static_cast<const WordTileState*>(state_);
      Player player = state->CurrentPlayer();
      if (player < 0) {
        auto a = std::make_unique<WordTileActionStruct>();
        a->type = "pass";
        return a;
      }
      // If in challenge phase, randomly accept or challenge
      if (state->phase_ == Phase::kChallenge) {
        auto a = std::make_unique<WordTileActionStruct>();
        a->type = (rng_() % 2 == 0) ? "accept" : "challenge";
        return a;
      }
      // Try to find a valid place move
      std::string rack = state->racks_[player];
      // Remove blank '?' from rack for simple sampler (skip blank handling)
      std::string rack_letters;
      for (char c : rack) if (c != '?') rack_letters.push_back(c);
      if (rack_letters.empty()) rack_letters = rack;  // fallback

      // Generate all unique words from rack permutations
      std::vector<std::string> candidate_words;
      std::string perm = rack_letters;
      std::sort(perm.begin(), perm.end());
      do {
        for (int len = 2; len <= std::min(7, (int)perm.size()); ++len) {
          std::string word = perm.substr(0, len);
          if (state->IsValidWord(word)) {
            candidate_words.push_back(word);
          }
        }
      } while (std::next_permutation(perm.begin(), perm.end()));
      // Deduplicate
      std::sort(candidate_words.begin(), candidate_words.end());
      candidate_words.erase(std::unique(candidate_words.begin(), candidate_words.end()), candidate_words.end());
      // Shuffle
      std::shuffle(candidate_words.begin(), candidate_words.end(), rng_);

      // Find anchor squares (empty squares adjacent to existing tiles, or center)
      std::vector<std::pair<int,int>> anchors;
      bool board_empty = true;
      for (int r = 0; r < state->board_size_; ++r) {
        for (int c = 0; c < state->board_size_; ++c) {
          if (state->board_[r][c] != ' ') { board_empty = false; break; }
        }
      }
      if (board_empty) {
        int center = state->board_size_ / 2;
        anchors.push_back({center, center});
      } else {
        for (int r = 0; r < state->board_size_; ++r) {
          for (int c = 0; c < state->board_size_; ++c) {
            if (state->board_[r][c] != ' ') continue;
            // Check neighbor has tile
            const int dr[4] = {-1,1,0,0};
            const int dc[4] = {0,0,-1,1};
            bool neighbor = false;
            for (int k=0;k<4;k++) {
              int nr = r + dr[k], nc = c + dc[k];
              if (nr>=0 && nr < state->board_size_ && nc>=0 && nc < state->board_size_) {
                if (state->board_[nr][nc] != ' ') { neighbor = true; break; }
              }
            }
            if (neighbor) anchors.push_back({r,c});
          }
        }
      }
      std::shuffle(anchors.begin(), anchors.end(), rng_);

      // Try candidate words at anchor positions
      for (const std::string& word : candidate_words) {
        int len = word.size();
        for (auto [ar, ac] : anchors) {
          for (int direction = 0; direction < 2; ++direction) {
            int dr = (direction == 1) ? 1 : 0;
            int dc = (direction == 0) ? 1 : 0;
            // Try all offsets where word covers anchor
            for (int offset = 0; offset < len; ++offset) {
              int start_r = ar - dr * offset;
              int start_c = ac - dc * offset;
              int end_r = start_r + dr * (len - 1);
              int end_c = start_c + dc * (len - 1);
              if (start_r < 0 || start_c < 0 || end_r >= state->board_size_ || end_c >= state->board_size_) continue;
              // Build tiles_played string
              std::string tiles_played;
              bool valid_placement = true;
              for (int i = 0; i < len; ++i) {
                int r = start_r + dr * i;
                int c = start_c + dc * i;
                char board_char = state->board_[r][c];
                char word_char = word[i];
                if (board_char == ' ') {
                  tiles_played.push_back(word_char);
                } else if (board_char == word_char) {
                  tiles_played.push_back('.');
                } else {
                  valid_placement = false;
                  break;
                }
              }
              if (!valid_placement) continue;
              // Check at least one tile played
              bool has_new = tiles_played.find_first_not_of('.') != std::string::npos;
              if (!has_new) continue;
              // Construct action struct
              auto action = std::make_unique<WordTileActionStruct>();
              action->type = "place";
              action->start_row = start_r;
              action->start_col = start_c;
              action->direction = direction;
              action->word = word;
              action->tiles_played = tiles_played;
              action->blank_assignments = "";
              // Validate
              std::string err = state->ValidatePlaceAction(*action, player);
              if (err.empty()) {
                return action;
              }
            }
          }
        }
      }
      // No valid place found, try exchange if bag has tiles
      int bag_total = 0;
      for (int c : state->bag_counts_) bag_total += c;
      if (bag_total >= 7 && !rack.empty()) {
        // Exchange up to 3 random tiles
        int n_exchange = std::min(3, (int)rack.size());
        std::string exchange_tiles = rack.substr(0, n_exchange);
        auto a = std::make_unique<WordTileActionStruct>();
        a->type = "exchange";
        a->exchange_tiles = exchange_tiles;
        return a;
      }
      // Fall back to pass
      auto a = std::make_unique<WordTileActionStruct>();
      a->type = "pass";
      return a;
    }
   private:
    mutable std::mt19937 rng_;
  };
  return std::make_unique<RandomSampler>(this, seed);
}

// private helpers
bool WordTileState::IsValidWord(const std::string& word) const {
  if (word.size() < 2) return false;  // Scrabble minimum 2 letters, though allow 1 for testing?
  return game_->IsValidWord(word);
}

// Helper to get board char with bounds check
char GetBoardChar(const std::vector<std::vector<char>>& board, int r, int c) {
  if (r < 0 || r >= (int)board.size() || c < 0 || c >= (int)board[0].size()) return ' ';
  return board[r][c];
}

std::vector<std::string> WordTileState::FormedWordsFromPlay(const WordTileActionStruct& a) const {
  std::vector<std::string> words;
  int len = a.word.size();
  if (len == 0) return words;
  int dr = (a.direction == 1) ? 1 : 0;
  int dc = (a.direction == 0) ? 1 : 0;

  // Main word
  words.push_back(a.word);

  // Cross words for each newly placed tile
  for (int i = 0; i < len; ++i) {
    char played = (i < (int)a.tiles_played.size()) ? a.tiles_played[i] : '.';
    if (played == '.') continue;  // existing tile, cross word already existed
    int r = a.start_row + dr * i;
    int c = a.start_col + dc * i;
    // Perpendicular direction
    int pdr = dc;
    int pdc = dr;
    // Find start of cross word
    int cr = r - pdr;
    int cc = c - pdc;
    std::string prefix;
    while (GetBoardChar(board_, cr, cc) != ' ') {
      prefix = GetBoardChar(board_, cr, cc) + prefix;
      cr -= pdr;
      cc -= pdc;
    }
    // Find end of cross word
    cr = r + pdr;
    cc = c + pdc;
    std::string suffix;
    while (GetBoardChar(board_, cr, cc) != ' ') {
      suffix += GetBoardChar(board_, cr, cc);
      cr += pdr;
      cc += pdc;
    }
    std::string cross_word = prefix + std::string(1, std::toupper(a.word[i])) + suffix;
    if (cross_word.size() > 1) {
      words.push_back(cross_word);
    }
  }
  return words;
}

int WordTileState::ScorePlay(const WordTileActionStruct& a) const {
  int len = a.word.size();
  if (len == 0) return 0;
  int dr = (a.direction == 1) ? 1 : 0;
  int dc = (a.direction == 0) ? 1 : 0;

  auto blank_map = ParseBlankAssignments(a.blank_assignments);

  // Helper to score a single word given its positions
  auto score_word_at = [&](int start_r, int start_c, int wdr, int wdc, int word_len,
                           const std::vector<bool>& is_new,
                           const std::vector<char>& letters,
                           const std::vector<bool>& is_blank) -> int {
    int letter_sum = 0;
    int word_multiplier = 1;
    for (int i = 0; i < word_len; ++i) {
      int r = start_r + wdr * i;
      int c = start_c + wdc * i;
      char letter = letters[i];
      bool is_new_tile = is_new[i];
      bool blank_tile = is_blank[i];
      int letter_value = blank_tile ? 0 : LetterValue(letter);
      int letter_mult = 1;
      int word_mult = 1;
      if (is_new_tile && r >= 0 && r < board_size_ && c >= 0 && c < board_size_) {
        Premium prem = premiums_[r][c];
        switch (prem) {
          case Premium::kDoubleLetter: letter_mult = 2; break;
          case Premium::kTripleLetter: letter_mult = 3; break;
          case Premium::kDoubleWord: word_mult = 2; break;
          case Premium::kTripleWord: word_mult = 3; break;
          default: break;
        }
      }
      letter_sum += letter_value * letter_mult;
      word_multiplier *= word_mult;
    }
    return letter_sum * word_multiplier;
  };

  int total_score = 0;
  int tiles_played_count = 0;

  // Score main word
  {
    std::vector<bool> is_new(len);
    std::vector<char> letters(len);
    std::vector<bool> is_blank(len, false);
    for (int i = 0; i < len; ++i) {
      int r = a.start_row + dr * i;
      int c = a.start_col + dc * i;
      char board_char = GetBoardChar(board_, r, c);
      char word_char = std::toupper(a.word[i]);
      char played = (i < (int)a.tiles_played.size()) ? a.tiles_played[i] : '.';
      letters[i] = word_char;
      if (played == '.') {
        is_new[i] = false;
        // Check if existing board tile was a blank
        int key = r * board_size_ + c;
        auto it = blank_assignments_board_.find(key);
        is_blank[i] = (it != blank_assignments_board_.end());
      } else {
        is_new[i] = true;
        tiles_played_count++;
        is_blank[i] = (played == '?');
      }
    }
    total_score += score_word_at(a.start_row, a.start_col, dr, dc, len, is_new, letters, is_blank);
  }

  // Score cross words
  for (int i = 0; i < len; ++i) {
    char played = (i < (int)a.tiles_played.size()) ? a.tiles_played[i] : '.';
    if (played == '.') continue;
    int r = a.start_row + dr * i;
    int c = a.start_col + dc * i;
    int pdr = dc;
    int pdc = dr;
    // Find start
    int cr = r;
    int cc = c;
    while (GetBoardChar(board_, cr - pdr, cc - pdc) != ' ') {
      cr -= pdr;
      cc -= pdc;
    }
    // Build cross word
    std::string cross_letters;
    std::vector<bool> is_new;
    std::vector<bool> is_blank;
    int tr = cr;
    int tc = cc;
    while (true) {
      char board_char = GetBoardChar(board_, tr, tc);
      bool is_center = (tr == r && tc == c);
      if (board_char == ' ' && !is_center) break;
      char letter;
      bool new_tile = false;
      bool blank_tile = false;
      if (is_center) {
        letter = std::toupper(a.word[i]);
        new_tile = true;
        blank_tile = (played == '?');
      } else {
        letter = board_char;
        new_tile = false;
        int key = tr * board_size_ + tc;
        auto it = blank_assignments_board_.find(key);
        blank_tile = (it != blank_assignments_board_.end());
      }
      cross_letters.push_back(letter);
      is_new.push_back(new_tile);
      is_blank.push_back(blank_tile);
      tr += pdr;
      tc += pdc;
      if (cross_letters.size() > board_size_) break;  // safety
      if (board_char == ' ' && !is_center) break;
      // Actually need to check next char; loop condition handles it
      if (GetBoardChar(board_, tr, tc) == ' ' && !(tr == r && tc == c)) {
        // Check if we've passed the center and next is empty
        // Simpler: continue while current position has a tile or is center
        // We already added current, now check next iteration will break if empty
      }
      // Break if next is empty and we've moved past center - handled at top of loop
      if (board_char == ' ' && !is_center) break;
      // Actually this loop is messy, let's redo simpler:
      break; // placeholder to avoid infinite - will fix below
    }
    // Simpler cross word scoring: reconstruct properly
    // Find full extent
    cr = r; cc = c;
    while (GetBoardChar(board_, cr - pdr, cc - pdc) != ' ') { cr -= pdr; cc -= pdc; }
    int start_r = cr;
    int start_c = cc;
    cross_letters.clear();
    is_new.clear();
    is_blank.clear();
    tr = start_r; tc = start_c;
    while (true) {
      bool is_center = (tr == r && tc == c);
      char board_char = GetBoardChar(board_, tr, tc);
      if (board_char == ' ' && !is_center) break;
      char letter;
      bool new_tile, blank_tile;
      if (is_center) {
        letter = std::toupper(a.word[i]);
        new_tile = true;
        blank_tile = (played == '?');
      } else {
        letter = board_char;
        new_tile = false;
        int key = tr * board_size_ + tc;
        auto it = blank_assignments_board_.find(key);
        blank_tile = (it != blank_assignments_board_.end());
      }
      cross_letters.push_back(letter);
      is_new.push_back(new_tile);
      is_blank.push_back(blank_tile);
      tr += pdr;
      tc += pdc;
      if ((int)cross_letters.size() > board_size_) break;
    }
    if (cross_letters.size() > 1) {
      std::vector<char> letters_vec(cross_letters.begin(), cross_letters.end());
      total_score += score_word_at(start_r, start_c, pdr, pdc, cross_letters.size(), is_new, letters_vec, is_blank);
    }
  }

  // Bingo bonus
  if (tiles_played_count >= rack_size_) {
    total_score += kBingoBonus;
  }
  return total_score;
}

std::string WordTileState::ValidatePlaceAction(const WordTileActionStruct& a, Player player) const {
  // Basic bounds and consistency checks
  if (a.start_row < 0 || a.start_row >= board_size_ ||
      a.start_col < 0 || a.start_col >= board_size_) {
    return "Start position out of bounds";
  }
  if (a.direction != 0 && a.direction != 1) {
    return "Invalid direction";
  }
  int len = a.word.size();
  if (len == 0) return "Empty word";
  if ((int)a.tiles_played.size() != len) {
    return "tiles_played length must match word length";
  }
  int dr = (a.direction == 1) ? 1 : 0;
  int dc = (a.direction == 0) ? 1 : 0;
  int end_r = a.start_row + dr * (len - 1);
  int end_c = a.start_col + dc * (len - 1);
  if (end_r < 0 || end_r >= board_size_ || end_c < 0 || end_c >= board_size_) {
    return "Word extends off board";
  }

  // Check board squares match, count tiles played, check connectivity
  int tiles_played_count = 0;
  bool touches_existing = false;
  bool covers_center = false;
  const int center = board_size_ / 2;
  for (int i = 0; i < len; ++i) {
    int r = a.start_row + dr * i;
    int c = a.start_col + dc * i;
    char board_char = board_[r][c];
    char word_char = std::toupper(a.word[i]);
    if (word_char < 'A' || word_char > 'Z') {
      return "Word contains invalid character";
    }
    char played = a.tiles_played[i];
    if (played == '.') {
      // Must match existing board tile
      if (board_char == ' ') return "tiles_played '.' but board square empty";
      if (board_char != word_char) return "Board letter mismatch";
      touches_existing = true;
    } else {
      // Playing a new tile
      if (board_char != ' ') return "Trying to play on occupied square";
      if (played != '?' && played != word_char) {
        return "tiles_played char must match word char or be '?' for blank";
      }
      tiles_played_count++;
      // Check orthogonal neighbors for connectivity
      int pdr = dc;
      int pdc = dr;
      if (GetBoardChar(board_, r - pdr, c - pdc) != ' ' ||
          GetBoardChar(board_, r + pdr, c + pdc) != ' ') {
        touches_existing = true;
      }
    }
    if (r == center && c == center) covers_center = true;
    // Check adjacent in main direction for connectivity (overlap already counted)
  }

  if (tiles_played_count == 0) {
    return "Must play at least one tile";
  }

  // Check rack has tiles
  if (!RackHasTiles(player, a.tiles_played)) {
    return "Rack does not contain required tiles";
  }

  // Check first move covers center, subsequent moves touch existing
  bool board_empty = BoardIsEmpty(board_);
  if (board_empty) {
    if (!covers_center) return "First move must cover center square";
  } else {
    if (!touches_existing) return "Play must connect to existing tiles";
  }

  // Check squares before start and after end are empty (word is maximal)
  int before_r = a.start_row - dr;
  int before_c = a.start_col - dc;
  if (GetBoardChar(board_, before_r, before_c) != ' ') {
    return "Letter immediately before start - word not maximal";
  }
  int after_r = end_r + dr;
  int after_c = end_c + dc;
  if (GetBoardChar(board_, after_r, after_c) != ' ') {
    return "Letter immediately after end - word not maximal";
  }

  // Validate blank assignments match tiles_played
  auto blank_map = ParseBlankAssignments(a.blank_assignments);
  for (int i = 0; i < len; ++i) {
    char played = a.tiles_played[i];
    bool is_blank_pos = blank_map.find(i) != blank_map.end();
    if (played == '?') {
      if (!is_blank_pos) return "Blank tile played but no blank_assignment";
      char assigned = blank_map[i];
      if (assigned != std::toupper(a.word[i])) {
        return "Blank assignment does not match word letter";
      }
    } else {
      if (is_blank_pos) return "Blank assignment for non-blank tile";
    }
  }
  if ((int)blank_map.size() != std::count(a.tiles_played.begin(), a.tiles_played.end(), '?')) {
    return "blank_assignments count mismatch tiles_played '?' count";
  }

  // Check all formed words are valid
  // Temporarily apply tiles to board to check cross words correctly?
  // FormedWordsFromPlay uses current board_ (without the new tiles) for cross word prefix/suffix,
  // which is correct because it includes existing board letters but not the new tile itself except main word.
  // Actually FormedWordsFromPlay as implemented includes the new tile letter from a.word, and existing board letters,
  // so it should be correct.
  std::vector<std::string> formed = FormedWordsFromPlay(a);
  for (const std::string& w : formed) {
    if (!IsValidWord(w)) {
      return std::string("Invalid word formed: ") + w;
    }
  }

  return "";  // valid
}

void WordTileState::DealInitialRacks() {}
void WordTileState::DrawTilesForPlayer(Player p, int count) {}

void WordTileState::FinalizeScores() {
  // Subtract remaining rack tiles from each player's score
  std::array<int, 2> rack_values = {0, 0};
  for (int p = 0; p < 2; ++p) {
    int value = 0;
    for (char c : racks_[p]) {
      value += LetterValue(c);  // blank '?' returns 0
    }
    rack_values[p] = value;
    scores_[p] -= value;
  }
  // If one player has empty rack, add opponent's rack value to finisher
  // (opponent already subtracted above, so this gives the standard 2x swing)
  if (racks_[0].empty() && !racks_[1].empty()) {
    scores_[0] += rack_values[1];
  } else if (racks_[1].empty() && !racks_[0].empty()) {
    scores_[1] += rack_values[0];
  }
  // If both racks empty or both non-empty (pass ending), no bonus – scores already include rack subtraction
}

bool WordTileState::RackHasTiles(Player p, const std::string& needed) const {
  std::array<int, kNumTileTypes> rack_counts{};
  for (char c : racks_[p]) {
    int idx = LetterToIndex(c);
    if (idx >= 0) rack_counts[idx]++;
  }
  std::array<int, kNumTileTypes> need_counts{};
  for (char c : needed) {
    if (c == '.') continue;  // board tile, not from rack
    int idx = LetterToIndex(c);
    if (idx < 0) return false;
    need_counts[idx]++;
  }
  for (int i = 0; i < kNumTileTypes; ++i) {
    if (need_counts[i] > rack_counts[i]) return false;
  }
  return true;
}

void WordTileState::RemoveTilesFromRack(Player p, const std::string& needed) {
  std::string& rack = racks_[p];
  for (char c : needed) {
    if (c == '.') continue;
    // Find exact tile in rack and remove it
    size_t pos = rack.find(c);
    if (pos == std::string::npos) {
      // Should have been validated by RackHasTiles
      SpielFatalError(absl::StrCat("Rack missing tile ", std::string(1, c)));
    }
    rack.erase(pos, 1);
  }
}

void WordTileState::AddTilesToRack(Player p, const std::string& tiles) {
  racks_[p] += tiles;
}

void WordTileState::AddTilesToBag(const std::string& tiles) {
  for (char c : tiles) {
    int idx = LetterToIndex(c);
    if (idx >= 0) bag_counts_[idx]++;
  }
}

}  // namespace word_tile
}  // namespace open_spiel
