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

}  // namespace

// ---- WordTileGame ----
WordTileGame::WordTileGame(const GameParameters& params)
    : Game(kGameType, params),
      dictionary_path_(::open_spiel::ParameterValue<std::string>(params, "dictionary_file", std::string(""))),
      board_size_(::open_spiel::ParameterValue<int>(params, "board_size", kDefaultBoardSize)),
      rack_size_(::open_spiel::ParameterValue<int>(params, "rack_size", kDefaultRackSize)) {
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
  current_player_ = 0;
  phase_ = Phase::kDealInitial;
  // We'll transition to chance node immediately; OpenSpiel expects initial state to be chance if needed.
  // For simplicity stub, set phase to Play and give dummy racks.
  racks_[0] = "AAAAAAA";
  racks_[1] = "BBBBBBB";
  phase_ = Phase::kPlay;
}

void WordTileState::InitializeBoardPremiums() {
  premiums_ = CreateStandardBoard();
}

void WordTileState::InitializeTileBag() {
  for (int i = 0; i < kNumTileTypes; ++i) {
    bag_counts_[i] = kTileDistribution[i];
  }
  // Remove dummy rack tiles for stub
  for (char c : racks_[0]) { int idx = LetterToIndex(c); if(idx>=0) bag_counts_[idx]--; }
  for (char c : racks_[1]) { int idx = LetterToIndex(c); if(idx>=0) bag_counts_[idx]--; }
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
  // Stub: single dummy outcome for now to allow compilation. Real implementation will enumerate tile draws.
  return {{0, 1.0}};
}

std::vector<Action> WordTileState::LegalActions() const {
  // Action structs only game returns empty vector; actual legal actions via action struct interface not yet implemented.
  // For stub, return empty to indicate use action structs.
  return {};
}

std::string WordTileState::ActionToString(Player player, Action action) const {
  return "stub";
}

std::string WordTileState::ToString() const {
  std::string s = "Word Tile Game stub board 15x15\n";
  s += "Scores: P0=" + std::to_string(scores_[0]) + " P1=" + std::to_string(scores_[1]) + "\n";
  s += "Current player: " + std::to_string(current_player_) + " phase " + std::to_string(static_cast<int>(phase_)) + "\n";
  s += "Rack0: " + racks_[0] + " Rack1: " + racks_[1] + "\n";
  // simple board view top 3 rows for brevity
  for (int r=0;r<3;r++){ for(int c=0;c<board_size_;c++) s+= board_[r][c]==' ' ? '.' : board_[r][c]; s+="\n"; }
  s+="...\n";
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
  s += "Scores " + std::to_string(scores_[0]) + " " + std::to_string(scores_[1]) + "\n";
  s += "Bag total: ";
  int total=0; for(int c: bag_counts_) total+=c; s+= std::to_string(total) + "\n";
  s += ToString();
  return s;
}

std::string WordTileState::ObservationString(Player player) const {
  return InformationStateString(player);
}

void WordTileState::ObservationTensor(Player player, absl::Span<float> values) const {
  // Fill zeros for stub
  std::fill(values.begin(), values.end(), 0.0f);
}

std::unique_ptr<State> WordTileState::Clone() const {
  return std::unique_ptr<State>(new WordTileState(*this));
}

void WordTileState::UndoAction(Player player, Action action) {
  SpielFatalError("Undo not implemented for stub");
}

void WordTileState::DoApplyAction(Action action) {
  SpielFatalError("Flat actions not supported, use action struct");
}

void WordTileState::DoApplyAction(const WordTileActionStruct& a) {
  // Stub implementation: only handle pass action to allow game progression for testing plumbing
  if (a.type == "pass") {
    consecutive_scoreless_turns_++;
    if (consecutive_scoreless_turns_ >= 6) {
      phase_ = Phase::kGameOver;
    } else {
      current_player_ = 1 - current_player_;
    }
    return;
  }
  if (a.type == "exchange") {
    consecutive_scoreless_turns_++;
    current_player_ = 1 - current_player_;
    return;
  }
  if (a.type == "accept") {
    // commit pending play - stub just switch turn
    pending_play_ = false;
    current_player_ = 1 - pending_player_;
    consecutive_scoreless_turns_ = 0;
    return;
  }
  if (a.type == "challenge") {
    // stub always accept challenge as valid for now -> challenger loses turn
    pending_play_ = false;
    // challenger is current player in challenge phase, which is opponent of pending player
    // challenger loses turn, so turn goes back to pending player
    current_player_ = pending_player_;
    consecutive_scoreless_turns_++;
    return;
  }
  if (a.type == "place") {
    // Very simplified stub: place first letter of word at start position, score 1, go to challenge phase
    if (a.start_row >=0 && a.start_col >=0 && a.start_row < board_size_ && a.start_col < board_size_) {
      board_[a.start_row][a.start_col] = a.word.empty() ? 'A' : std::toupper(a.word[0]);
    }
    pending_play_ = true;
    pending_player_ = current_player_;
    pending_score_ = 1;
    scores_[current_player_] += 1;
    phase_ = Phase::kChallenge;
    return;
  }
  SpielFatalError("Unknown action type in stub");
}

std::unique_ptr<ActionStruct> WordTileState::ActionToStruct(Player player, Action action_id) const {
  SpielFatalError("Not supported in stub");
  return nullptr;
}
std::vector<Action> WordTileState::StructToActions(const ActionStruct& action_struct) const {
  return {0}; // dummy single flat action mapping for action-struct-only interface compliance
}
Status WordTileState::ApplyActionStruct(const ActionStruct& action_struct) {
  const auto* a = dynamic_cast<const WordTileActionStruct*>(&action_struct);
  if (!a) return ErrorStatus("Expected WordTileActionStruct");
  // Validate basic fields minimally for stub
  if (a->type != "place" && a->type != "exchange" && a->type != "pass" && a->type != "challenge" && a->type != "accept") {
    return ErrorStatus("Invalid action type");
  }
  // Transition logic simplified
  if (phase_ == Phase::kPlay) {
    if (a->type == "place") {
      DoApplyAction(*a);
      return OkStatus();
    } else if (a->type == "exchange" || a->type == "pass") {
      DoApplyAction(*a);
      return OkStatus();
    } else {
      return ErrorStatus("Challenge not allowed in Play phase");
    }
  } else if (phase_ == Phase::kChallenge) {
    if (a->type == "challenge" || a->type == "accept") {
      DoApplyAction(*a);
      // After challenge resolution, go back to play phase unless game over
      if (!IsTerminal()) phase_ = Phase::kPlay;
      return OkStatus();
    } else {
      return ErrorStatus("Only challenge or accept allowed in challenge phase");
    }
  }
  return ErrorStatus("Invalid phase for action");
}
Status WordTileState::ValidateActionStruct(const ActionStruct& action_struct) const {
  const auto* a = dynamic_cast<const WordTileActionStruct*>(&action_struct);
  if (!a) return ErrorStatus("Wrong type");
  if (phase_ == Phase::kPlay && (a->type=="place"||a->type=="exchange"||a->type=="pass")) return OkStatus();
  if (phase_ == Phase::kChallenge && (a->type=="challenge"||a->type=="accept")) return OkStatus();
  return ErrorStatus("Invalid action for phase");
}
std::unique_ptr<ActionStructSampler> WordTileState::GetActionStructSampler(int seed) const {
  // Stub sampler returning pass action always for now
  class StubSampler : public ActionStructSampler {
   public:
    explicit StubSampler(const State* s, int seed) : ActionStructSampler(s, seed) {}
    std::unique_ptr<ActionStruct> SampleActionStruct() override {
      auto a = std::make_unique<WordTileActionStruct>();
      a->type = "pass";
      return a;
    }
  };
  return std::make_unique<StubSampler>(this, seed);
}

// private helpers stub implementations
bool WordTileState::IsValidWord(const std::string& word) const { return game_->IsValidWord(word); }
std::vector<std::string> WordTileState::FormedWordsFromPlay(const WordTileActionStruct& a) const { return {a.word}; }
int WordTileState::ScorePlay(const WordTileActionStruct& a) const { return 1; }
void WordTileState::DealInitialRacks() {}
void WordTileState::DrawTilesForPlayer(Player p, int count) {}
bool WordTileState::RackHasTiles(Player p, const std::string& needed) const { return true; }
void WordTileState::RemoveTilesFromRack(Player p, const std::string& needed) {}
void WordTileState::AddTilesToRack(Player p, const std::string& tiles) {}
void WordTileState::AddTilesToBag(const std::string& tiles) {}

}  // namespace word_tile
}  // namespace open_spiel
