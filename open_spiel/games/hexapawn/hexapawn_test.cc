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

#include "open_spiel/games/hexapawn/hexapawn.h"

#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/basic_tests.h"

namespace open_spiel {
namespace hexapawn {
namespace {

namespace testing = open_spiel::testing;

void BasicHexapawnTests() {
  testing::LoadGameTest("hexapawn");
  testing::NoChanceOutcomesTest(*LoadGame("hexapawn"));
  testing::RandomSimTest(*LoadGame("hexapawn"), 100);
}

void TestInitialState() {
  auto game = LoadGame("hexapawn");
  auto state = game->NewInitialState();
  // Initial board: bbb / ... / www
  SPIEL_CHECK_EQ(state->ToString(), "bbb\n...\nwww");
  SPIEL_CHECK_EQ(state->CurrentPlayer(), 0);
  auto actions = state->LegalActions();
  // White has 3 pawns, each can move forward if empty: should be 3 actions initially? Actually middle pawn blocked? No, board empty middle row, so 3 forward moves.
  // Plus no captures initially.
  SPIEL_CHECK_EQ(actions.size(), 3);
}

void TestWinByAdvancement() {
  auto game = LoadGame("hexapawn");
  // Sequence leading to white win by advancement: a2-a3 is illegal initially because black occupies a3? Wait board orientation.
  // Let's craft via state struct: white pawn at a2 (row1 col0), empty a3 (row0 col0), white to move.
  std::string json = R"({"board":[".","b","b","w",".",".",".","w","w"],"current_player":"w"})";
  auto state = game->NewInitialState(json);
  // White at a2 should move to a3 and win.
  // Find action a2-a3
  Action target = -1;
  for (Action a : state->LegalActions()) {
    if (state->ActionToString(0, a) == "a2-a3") target = a;
  }
  SPIEL_CHECK_NE(target, -1);
  state->ApplyAction(target);
  SPIEL_CHECK_TRUE(state->IsTerminal());
  auto returns = state->Returns();
  SPIEL_CHECK_EQ(returns[0], 1.0);
}

void TestWinByCaptureAll() {
  auto game = LoadGame("hexapawn");
  // Black has no pawns, white to move should be terminal white win.
  std::string json = R"({"board":[".",".",".",".",".",".","w","w","w"],"current_player":"b"})";
  auto state = game->NewInitialState(json);
  SPIEL_CHECK_TRUE(state->IsTerminal());
  SPIEL_CHECK_EQ(state->Returns()[0], 1.0);
}

void TestActionStruct() {
  auto game = LoadGame("hexapawn");
  auto state = game->NewInitialState();
  // First legal action should be something like a2-a3? Actually initial white pawns at row2: a1,b1,c1 moving to a2,b2,c2.
  // So a1-a2
  Action action_id = state->LegalActions()[0];
  auto* hp_state = static_cast<HexapawnState*>(state.get());
  auto action_struct = hp_state->ActionToStruct(0, action_id);
  // Validate round trip
  auto actions = hp_state->StructToActions(*action_struct);
  SPIEL_CHECK_EQ(actions.size(), 1);
  SPIEL_CHECK_EQ(actions[0], action_id);
}

}  // namespace
}  // namespace hexapawn
}  // namespace open_spiel

int main(int argc, char** argv) {
  open_spiel::hexapawn::BasicHexapawnTests();
  open_spiel::hexapawn::TestInitialState();
  open_spiel::hexapawn::TestWinByAdvancement();
  open_spiel::hexapawn::TestWinByCaptureAll();
  open_spiel::hexapawn::TestActionStruct();
}
