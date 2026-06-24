#!/usr/bin/env python3
"""Play Word Tile (Scrabble) in terminal.

Usage:
  python play_word_tile.py --player1 human --player2 random
"""

import random
import sys
from absl import app
from absl import flags
import pyspiel

FLAGS = flags.FLAGS
flags.DEFINE_enum("player1", "human", ["human", "random"], "Player 0 type")
flags.DEFINE_enum("player2", "random", ["human", "random"], "Player 1 type")
flags.DEFINE_string(
    "dictionary",
    "/home/denniscahillane/open_spiel/open_spiel/games/word_tile/data/enable.txt",
    "Dictionary file",
)
flags.DEFINE_integer("seed", 0, "Random seed")


def get_human_action(game, state):
    print(
        "\nYour rack:",
        state.information_state_string(state.current_player())
        .split("My rack:")[1]
        .split("\n")[0]
        if "My rack:" in state.information_state_string(state.current_player())
        else "?",
    )
    print("Enter action JSON, or 'pass', or 'exchange TILES', or 'challenge', 'accept'")
    print(
        'Place example: {"type":"place","start_row":7,"start_col":7,"direction":0,"word":"HELLO","tiles_played":"HELLO","blank_assignments":"","exchange_tiles":""}'
    )
    print(
        "  direction: 0=horizontal, 1=vertical; tiles_played uses '.' for existing board letters, '?' for blank"
    )
    while True:
        s = input("> ").strip()
        if not s:
            continue
        if s == "pass":
            js = '{"type":"pass","direction":0,"exchange_tiles":"","start_col":-1,"start_row":-1,"tiles_played":"","word":"","blank_assignments":""}'
        elif s.startswith("exchange "):
            tiles = s.split(" ", 1)[1].strip().upper()
            js = f'{{"type":"exchange","direction":0,"exchange_tiles":"{tiles}","start_col":-1,"start_row":-1,"tiles_played":"","word":"","blank_assignments":""}}'
        elif s == "challenge":
            js = '{"type":"challenge","direction":0,"exchange_tiles":"","start_col":-1,"start_row":-1,"tiles_played":"","word":"","blank_assignments":""}'
        elif s == "accept":
            js = '{"type":"accept","direction":0,"exchange_tiles":"","start_col":-1,"start_row":-1,"tiles_played":"","word":"","blank_assignments":""}'
        else:
            js = s
        # Try to parse JSON and fill in missing fields with defaults
        try:
            import json

            obj = json.loads(js)
            # Fill defaults for all WordTileActionStruct fields
            defaults = {
                "type": "pass",
                "start_row": -1,
                "start_col": -1,
                "direction": 0,
                "word": "",
                "tiles_played": "",
                "blank_assignments": "",
                "exchange_tiles": "",
            }
            for k, v in defaults.items():
                obj.setdefault(k, v)
            js = json.dumps(obj)
        except Exception:
            pass  # Let game.ActionStruct handle parse errors
        try:
            action = game.ActionStruct(js)
        except Exception as e:
            print("Parse error:", e)
            print(
                "Hint: Make sure JSON includes ALL fields: type, start_row, start_col, direction, word, tiles_played, blank_assignments, exchange_tiles"
            )
            continue
        status = state.validate_action_struct(action)
        if "Ok" in str(status):
            return action
        print("Invalid action:", status)


def get_random_action(state, seed):
    sampler = state.get_action_struct_sampler(seed)
    return sampler.sample_action_struct()


def main(argv):
    random.seed(FLAGS.seed)
    game = pyspiel.load_game("word_tile", {"dictionary_file": FLAGS.dictionary})
    state = game.new_initial_state()
    # Deal initial tiles
    while state.is_chance_node():
        outcomes = state.chance_outcomes()
        actions, probs = zip(*outcomes)
        action = random.choices(actions, probs)[0]
        state.apply_action(action)
    print("Game start!")
    print(state)
    move_num = 0
    players = {0: FLAGS.player1, 1: FLAGS.player2}
    while not state.is_terminal():
        if state.is_chance_node():
            outcomes = state.chance_outcomes()
            actions, probs = zip(*outcomes)
            action = random.choices(actions, probs)[0]
            state.apply_action(action)
            continue
        current = state.current_player()
        ptype = players[current]
        print("\n" + "=" * 60)
        print(f"Move {move_num}, Player {current} ({ptype}) to act")
        # Show info state for current player (includes board, scores, rack – hides opponent rack)
        print(state.information_state_string(current))
        if ptype == "human":
            action_struct = get_human_action(game, state)
        else:
            action_struct = get_random_action(state, move_num + FLAGS.seed)
            print(f"Random bot plays: {action_struct.to_json()}")
            # Validate just in case
            st = state.validate_action_struct(action_struct)
            if "Ok" not in str(st):
                print("Bot produced invalid action, forcing pass:", st)
                action_struct = game.ActionStruct(
                    '{"type":"pass","direction":0,"exchange_tiles":"","start_col":-1,"start_row":-1,"tiles_played":"","word":"","blank_assignments":""}'
                )
        # Save state before action for challenge feedback
        prev_scores = state.returns() if state.is_terminal() else None
        try:
            # Try to get returns (will be [0,0] if non-terminal, need actual scores)
            # Parse scores from information_state_string instead
            import re

            info_before = state.information_state_string(current)
            # Match both "Scores 8 0" and "Scores: P0=8 P1=0", with optional negative
            m = re.search(r"Scores:?\s*(?:P0=)?(-?\d+)\s+(?:P1=)?(-?\d+)", info_before)
            if m:
                prev_scores = [int(m.group(1)), int(m.group(2))]
        except:
            prev_scores = None
        # Check if we're in challenge phase and capture pending info
        state_str_before = str(state)
        pending_before = "Pending play" in state_str_before
        # Extract pending player/score if available
        pending_player = None
        pending_score = 0
        if pending_before:
            m = re.search(r"Pending play by P(\d) score (-?\d+)", state_str_before)
            if m:
                pending_player = int(m.group(1))
                pending_score = int(m.group(2))

        status = state.apply_action_struct(action_struct)
        print("Apply status:", status)

        # Challenge feedback
        action_type = ""
        try:
            # action_struct is a WordTileActionStruct with .type attribute in Python bindings
            action_type = getattr(action_struct, "type", "")
            if not action_type:
                # Fall back to parsing JSON
                import json

                action_json = action_struct.to_json()
                action_obj = json.loads(action_json)
                action_type = action_obj.get("type", "")
        except:
            pass

        if action_type in ("challenge", "accept"):
            # Get scores after action
            info_after = state.information_state_string(current)
            m = re.search(r"Scores:?\s*(?:P0=)?(-?\d+)\s+(?:P1=)?(-?\d+)", info_after)
            if m:
                new_scores = [int(m.group(1)), int(m.group(2))]
                if prev_scores:
                    print(f"\n--- Challenge Resolution ---")
                    if action_type == "accept":
                        print(f"Player {current} ACCEPTED the play.")
                        if pending_player is not None:
                            print(
                                f"Player {pending_player}'s play stands, +{pending_score} points."
                            )
                    else:  # challenge
                        # Check if scores changed (challenge succeeded if challenger didn't lose points and defender lost points)
                        # Simpler: check if board reverted – look for pending play message gone
                        state_str_after = str(state)
                        if (
                            "Pending play" not in state_str_after
                            and new_scores == prev_scores
                        ):
                            # Score unchanged from before the original play? Hard to tell.
                            # Actually if challenge succeeds, defender loses the pending_score points
                            # If challenge fails, defender keeps points, challenger just loses turn (no score change from pre-challenge state)
                            # We don't have pre-play scores, only pre-challenge scores
                            # Best heuristic: if the player who was challenged (pending_player) now has LOWER score than before challenge,
                            # then challenge succeeded
                            pass
                        # Just report based on whether the challenged player's score decreased
                        # We need pre-play scores, which we don't have – use simple message
                        print(f"Player {current} CHALLENGED the play.")
                        # Try to infer result from score change since before challenge action
                        # prev_scores was scores BEFORE challenge/accept action (i.e., with pending play already scored)
                        if new_scores[
                            pending_player if pending_player is not None else 0
                        ] < (
                            prev_scores[pending_player]
                            if prev_scores and pending_player is not None
                            else 0
                        ):
                            print(f"Challenge SUCCEEDED – invalid word! Play removed.")
                        else:
                            print(f"Challenge FAILED – all words valid. Play stands.")
                    print(f"Scores: P0={new_scores[0]} P1={new_scores[1]}")
                    print(f"--- End Challenge ---\n")

        move_num += 1
    print("\nGame over!")
    print(state)
    print("Returns:", state.returns())


if __name__ == "__main__":
    app.run(main)
