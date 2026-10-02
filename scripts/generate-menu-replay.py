#!/usr/bin/env python3
"""Reduce an RCL match log to the anonymous replay the menus play back.

Input is the JSON event array served by the RCL watch site for one match
(GridposLog samples every ~0.1 s, OnlinePlayerLog, CycleDestroyLog, ...).
Output is a small text file holding only what the menu background draws:
cycle paths as timed corner points, per-life colours and estimated zones.

Player names, chat, ids and the match id are never written. Players become
indexes in order of first appearance.

    python3 scripts/generate-menu-replay.py MATCH_LOG.json replays/menu_fort.rclreplay
    python3 scripts/generate-menu-replay.py https://.../api/logs/ID?mode=fort OUT
"""

import gzip
import json
import sys
import urllib.request
from collections import defaultdict

KEEP_EVERY = 1.0      # seconds between kept samples on a straight
LIFE_GAP = 0.6        # a longer silence means the cycle died and respawned
ZONE_RADIUS = 40.0    # Fortress zones are not in the log; mirror the web player
MIN_ROUND = 20.0      # skip rounds too short to be worth showing

# 0-15 per channel, like the game's own colour settings
TEAM_COLOURS = {"gold": (15, 11, 2), "blue": (3, 7, 15), "red": (15, 3, 3),
                "green": (3, 13, 4)}
FALLBACK_COLOURS = [(15, 11, 2), (3, 7, 15), (15, 3, 3), (3, 13, 4)]


def load(source):
    if source.startswith(("http://", "https://")):
        request = urllib.request.Request(source, headers={"Accept-Encoding": "gzip"})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read()
    else:
        with open(source, "rb") as handle:
            data = handle.read()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    return json.loads(data)


def kind(event):
    return event.get("$type", "").split(",")[0].rsplit(".", 1)[-1]


def corners(a, b):
    """Points a cycle passed through between two axis-aligned samples."""
    ax, ay, bx, by = a["PosX"], a["PosY"], b["PosX"], b["PosY"]
    da, db = (a["DirX"], a["DirY"]), (b["DirX"], b["DirY"])
    if da == db:
        lateral = (bx - ax) if da[0] == 0 else (by - ay)
        if abs(lateral) < 0.05:
            return []
        # Two turns between samples: step sideways half way along.
        if da[0] != 0:
            mid = (ax + bx) / 2
            return [(mid, ay), (mid, by)]
        mid = (ay + by) / 2
        return [(ax, mid), (bx, mid)]
    if da[0] == -db[0] and da[1] == -db[1]:
        # Reversed: one sideways step at the old position.
        return [(ax, by)] if da[0] != 0 else [(bx, ay)]
    # One turn: leave along the old heading, arrive along the new one.
    return [(bx, ay)] if da[0] != 0 else [(ax, by)]


def path_points(samples):
    points = []
    last_kept = None
    for index, sample in enumerate(samples):
        t = sample["ElapsedTime"]
        here = (sample["PosX"], sample["PosY"])
        if index == 0:
            points.append((t, *here))
            last_kept = t
            continue
        previous = samples[index - 1]
        turn = corners(previous, sample)
        if turn:
            # Spread corner times over the interval by distance travelled.
            chain = [(previous["PosX"], previous["PosY"]), *turn, here]
            lengths = [abs(q[0] - p[0]) + abs(q[1] - p[1])
                       for p, q in zip(chain, chain[1:])]
            total = sum(lengths) or 1.0
            t0, run = previous["ElapsedTime"], 0.0
            for corner, length in zip(turn, lengths):
                run += length
                points.append((t0 + (t - t0) * run / total, *corner))
            points.append((t, *here))
            last_kept = t
        elif index == len(samples) - 1 or t - last_kept >= KEEP_EVERY:
            points.append((t, *here))
            last_kept = t
    return points


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    events = load(sys.argv[1])

    rounds = defaultdict(lambda: {"samples": defaultdict(list), "colour": {},
                                  "team": {}, "end": 0.0})
    for event in events:
        name = kind(event)
        entry = rounds[event.get("RoundId", "")]
        if name == "GridposLog":
            entry["samples"][event["Username"]].append(event)
            entry["team"][event["Username"]] = event["Team"]
            entry["end"] = max(entry["end"], event["ElapsedTime"])
        elif name == "OnlinePlayerLog":
            entry["colour"][event["Username"]] = (
                event["Red"], event["Green"], event["Blue"])

    players, teams, colours = {}, {}, {}
    xs, ys = [], []
    spawns = defaultdict(list)
    out_rounds = []
    for round_id in sorted(rounds):
        entry = rounds[round_id]
        if not entry["samples"] or entry["end"] < MIN_ROUND:
            continue
        lives = []
        for user, samples in entry["samples"].items():
            samples.sort(key=lambda s: s["ElapsedTime"])
            player = players.setdefault(user, len(players))
            team = teams.setdefault(entry["team"][user], len(teams))
            if user in entry["colour"]:
                colours[player] = entry["colour"][user]
            if samples[0]["ElapsedTime"] < 1.0:
                # Round-start grid only; respawns happen all over the arena.
                spawns[team].append((samples[0]["PosX"], samples[0]["PosY"]))
            life = [samples[0]]
            for sample in samples[1:]:
                if sample["ElapsedTime"] - life[-1]["ElapsedTime"] > LIFE_GAP:
                    lives.append((player, team, path_points(life)))
                    life = []
                life.append(sample)
            lives.append((player, team, path_points(life)))
        lives = [life for life in lives if len(life[2]) >= 2]
        for _, _, points in lives:
            xs.extend(p[1] for p in points)
            ys.extend(p[2] for p in points)
        lives.sort(key=lambda life: life[2][0][0])
        out_rounds.append((entry["end"], lives))

    if not out_rounds:
        sys.exit("no playable rounds in input")

    with open(sys.argv[2], "w", newline="\n") as out:
        out.write("RCLREPLAY 1\n")
        out.write("arena %.1f %.1f %.1f %.1f\n" % (min(xs), min(ys), max(xs), max(ys)))
        names = {index: name.lower() for name, index in teams.items()}
        for team in sorted(spawns):
            points = spawns[team]
            colour = next((rgb for key, rgb in TEAM_COLOURS.items()
                           if key in names[team]),
                          FALLBACK_COLOURS[team % len(FALLBACK_COLOURS)])
            out.write("zone %d %.1f %.1f %.1f %d %d %d\n" % (
                team,
                sum(p[0] for p in points) / len(points),
                sum(p[1] for p in points) / len(points),
                ZONE_RADIUS, *colour))
        count = 0
        for end, lives in out_rounds:
            out.write("round %.2f %d\n" % (end, len(lives)))
            for player, team, points in lives:
                red, green, blue = colours.get(player, (15, 15, 15))
                out.write("life %d %d %d %d %d %d\n" % (
                    player, team, red, green, blue, len(points)))
                for t, x, y in points:
                    out.write("%.2f %.1f %.1f\n" % (t, x, y))
                count += len(points)
    print("%s: %d rounds, %d players, %d points" % (
        sys.argv[2], len(out_rounds), len(players), count))


if __name__ == "__main__":
    main()
