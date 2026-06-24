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

#include "open_spiel/python/pybind11/games_word_tile.h"

#include <memory>
#include <string>
#include <utility>

#include "open_spiel/games/word_tile/word_tile.h"
#include "open_spiel/python/pybind11/pybind11.h"
#include "open_spiel/spiel.h"
#include "pybind11/include/pybind11/cast.h"
#include "pybind11/include/pybind11/pybind11.h"

namespace py = ::pybind11;
using open_spiel::ActionStruct;
using open_spiel::Game;
using open_spiel::State;
using open_spiel::word_tile::WordTileActionStruct;
using open_spiel::word_tile::WordTileGame;
using open_spiel::word_tile::WordTileState;

void open_spiel::init_pyspiel_games_word_tile(py::module& m) {
  py::module_ word_tile = m.def_submodule("word_tile");

  auto action_struct_cls =
      bind_spiel_struct<WordTileActionStruct, ActionStruct>(
          word_tile, "WordTileActionStruct")
      .def(py::init<>())
      .def(py::init<const std::string&>(), py::arg("json_str"))
      .def_readwrite("type", &WordTileActionStruct::type)
      .def_readwrite("start_row", &WordTileActionStruct::start_row)
      .def_readwrite("start_col", &WordTileActionStruct::start_col)
      .def_readwrite("direction", &WordTileActionStruct::direction)
      .def_readwrite("word", &WordTileActionStruct::word)
      .def_readwrite("tiles_played", &WordTileActionStruct::tiles_played)
      .def_readwrite("blank_assignments", &WordTileActionStruct::blank_assignments)
      .def_readwrite("exchange_tiles", &WordTileActionStruct::exchange_tiles);

  py::classh<WordTileState, State>(m, "WordTileState");

  auto word_tile_game =
      py::classh<WordTileGame, Game>(m, "WordTileGame");
  word_tile_game.attr("ActionStruct") = action_struct_cls;
}
