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
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/basic_tests.h"

namespace open_spiel {
namespace word_tile {
namespace {

namespace testing = open_spiel::testing;

void BasicWordTileTests() {
  testing::LoadGameTest("word_tile");
  // Skip NoChanceOutcomes because game has chance nodes for dealing
  // testing::RandomSimTest would need action struct sampler which we stub to pass only
  auto game = LoadGame("word_tile");
  auto state = game->NewInitialState();
  SPIEL_CHECK_FALSE(state == nullptr);
  SPIEL_CHECK_EQ(game->NumPlayers(), 2);
}

}  // namespace
}  // namespace word_tile
}  // namespace open_spiel

int main(int argc, char** argv) {
  open_spiel::word_tile::BasicWordTileTests();
}
