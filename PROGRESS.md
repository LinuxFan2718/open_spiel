# Word Tile (Scrabble-inspired) – OpenSpiel Integration Progress

**Date:** 2026-06-24
**Branch:** two_player_word_tile
**Game:** `open_spiel/games/word_tile/` – 2-player Scrabble clone

## Current Status: Playable Beta

The game is fully playable via `play_word_tile.py` (human vs random, random vs random). Core Scrabble mechanics are implemented and tested.

### Implemented

- **Game structure**: 2-player, zero-sum, imperfect information, 15×15 board, 100-tile English distribution, 7-tile rack
- **Phases**: DealInitial (chance) → Play → Challenge → Draw (chance) → GameOver
- **Action structs** (`action_structs_only=true`):
  - `place`: start_row/col, direction (0=H,1=V), word, tiles_played ('.' = existing board letter, '?' = blank), blank_assignments
  - `exchange`: exchange_tiles
  - `pass`, `challenge`, `accept`
- **Board**: standard Scrabble premium layout (DL/TL/DW/TW), center star = DW
- **Dictionary**: ENABLE word list (178k words), hash_set lookup + Trie for prefix pruning
- **Move validation**: bounds check, connectivity (first move must cover center), rack contains tiles, board letters match, all formed cross-words validated, word maximality check
- **Scoring**: letter values with DL/TL premiums, word multipliers (DW/TW), blank tiles score 0, 50-point bingo bonus for using all 7 rack tiles, cross-word scoring
- **Rack/bag management**: chance nodes for initial deal (14 sequential tile draws) and post-play draws, proper bag counts, tile return on successful challenge
- **Challenge resolution**: double-challenge rule – invalid play → tiles returned, challenger keeps turn, score reverted; valid play → challenger loses turn, defender keeps score and draws
- **Game end**: 6 consecutive scoreless turns → finalize scores (subtract remaining rack tiles); OR empty rack + empty bag → finalize scores + opponent rack bonus to finisher
- **Observation tensor**: 33 planes × 15×15 – empty squares, A-Z letters, blank tiles, DL/TL/DW/TW premiums
- **Python bindings**: `WordTileActionStruct` exposed via pybind11 (`game.ActionStruct(json_str)`), with read/write fields, JSON round-trip
- **Play script**: `play_word_tile.py` – terminal human vs random / random vs random, with challenge feedback, proper info-state hiding (opponent rack hidden)
- **Test**: `word_tile_test.cc` – LoadGameTest passes

### Known Limitations / TODO

- **Move generation**: `ActionStructSampler` uses brute-force rack permutation → dictionary lookup → board anchor fitting. Works for random bot play, but slow (~13k permutations per move), no blank tile support in sampler, no trie-accelerated move generation, no strategic move ordering
- **No flat action encoding**: `action_structs_only=true`, so standard OpenSpiel bots (MCTS, minimax, RL) that expect `legal_actions()` → `int` do not work out of the box. Use `get_action_struct_sampler()` / `apply_action_struct()` API instead. A `play_word_tile.py` wrapper provides human play.
- **Observation tensor**: board-only, no rack/bag/score channels, no history planes. Sufficient for basic CNN, but not full information state.
- **InformationStateTensor**: not implemented (`provides_information_state_tensor=false`)
- **UndoAction**: not implemented – use `Clone()` for tree search (MCTS default)
- **No endgame tile tracking**: opponent rack is hidden (correct for imperfect info), but no inference / tracking
- **Dictionary path**: defaults to `open_spiel/games/word_tile/data/enable.txt` with fallbacks, including absolute `/home/denniscahillane/...` path – should be made relocatable via runfiles
- **No time control / pass-exchange restrictions**: exchange allowed with <7 tiles in bag (should be disallowed per official rules – currently only warns)
- **No 2-letter word list optimization**, no board symmetry / anchor pruning, no leave evaluation, no bingo stem tracking – all left to future AI agent work
- **No Python game-specific module**: `pyspiel.word_tile.WordTileActionStruct` is exposed, but no convenience helpers (e.g., algebraic notation parser like "H8 HELLO")

### How to Play

```bash
cd /home/denniscahillane/open_spiel
PYTHONPATH=build/python:$PYTHONPATH python play_word_tile.py --player1=human --player2=random
```

Human input accepts JSON action structs, or shortcuts: `pass`, `exchange AEIOU`, `challenge`, `accept`

Place example:
```json
{"type":"place","start_row":7,"start_col":7,"direction":0,"word":"HELLO","tiles_played":"HELLO","blank_assignments":"","exchange_tiles":""}
```
- `direction`: 0=horizontal, 1=vertical
- `tiles_played`: '.' = existing board letter, '?' = blank tile, A-Z = normal tile from rack
- `blank_assignments`: e.g. `"2:J,5:S"` for blank tiles

### Build / Test

```bash
cd build
make -j$(nproc) word_tile_test pyspiel
./games/word_tile_test
PYTHONPATH=build/python:$PYTHONPATH python ../play_word_tile.py --player1=random --player2=random --seed=42
```

### Files

- `open_spiel/games/word_tile/word_tile.h/.cc` – Game and State, ~1200 LOC
- `open_spiel/games/word_tile/board.h` – premium layout
- `open_spiel/games/word_tile/tiles.h` – distribution, letter values
- `open_spiel/games/word_tile/dictionary.h/.cc` – ENABLE loader, Trie
- `open_spiel/games/word_tile/word_tile_test.cc` – LoadGameTest
- `open_spiel/games/word_tile/data/enable.txt` – ENABLE word list
- `open_spiel/python/pybind11/games_word_tile.{h,cc}` – Python bindings for `WordTileActionStruct`
- `play_word_tile.py` – terminal human/random player

### Next Steps

1. Implement proper move generator with trie + board anchor pruning (GADDAG), support blank tiles
2. Add rack / score / turn channels to observation tensor for RL training
3. Implement `InformationStateTensor` for CFR / imperfect-info algorithms
4. Add `UndoAction` for MCTS efficiency
5. Add endgame solver / rack leave evaluation
6. Integrate with OpenSpiel RL bots via action-struct wrapper
7. Add algebraic notation parser (`H8 HELLO`) for human-friendly input
8. Remove hardcoded absolute dictionary path, use runfiles / open_spiel_data
9. Add comprehensive tests: scoring with premiums, challenge resolution, bingo, end-game scoring, random sim test
10. Performance profiling – move generation is currently the bottleneck
