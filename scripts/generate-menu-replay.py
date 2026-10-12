#!/usr/bin/env python3
"""Reduce an RCL match log to the anonymous replay the menus play back.

Input is the JSON event array served by the RCL watch site for one Fortress
match (GridposLog samples about every 0.1 s and at every turn, with the
cycle's speed; OnlinePlayerLog; CycleDestroyLog; ConquerLog). Output is a small
text file holding only what the menu background draws: each cycle's path as
timed points, per-life colours, the two zones and when each fell.

Player names, chat, ids and the match id are never written. Players become
indexes in order of first appearance.

    python3 scripts/generate-menu-replay.py MATCH_LOG.json replays/menu_fort.rclreplay
    python3 scripts/generate-menu-replay.py https://.../api/logs/ID?mode=fort OUT

How the motion is rebuilt. The log's positions are exact where they matter:
a turn is logged at its corner, so the straight lines between samples are the
path the cycle rode (the few samples with a turn between them get the corner
worked out). The log's times are not: a turn's line carries the place of the
turn and a clock a little behind it, and the beat samples jitter by a tenth
of their spacing. Played back as logged, a cycle lunges into every corner and
crawls out of it. So time is put back along the path from the speed the log
gives at each sample (time = distance / speed), and only held to the log's
clock over stretches of a couple of seconds. Where a cycle stands against a
wall the log's own spacing is kept.
"""

import gzip
import json
import sys
import urllib.request
from collections import defaultdict

LIFE_GAP = 0.6        # a longer silence means the cycle died and respawned
MIN_ROUND = 20.0      # skip rounds too short to be worth showing
SLOW = 3.0            # under this speed a cycle is taken to be held by a wall
CLOCK_WINDOW = 21     # samples (about two seconds) the log's clock is averaged over
KEEP_WITHIN = 0.05    # how far a dropped point may lie from the line between those kept

# The map every RCL Fortress server plays, Z-Man/fortress/for_old_clients-0.1.0:
# a 500 square, a zone of radius 40 around each team's spawn point.
ARENA = (0.0, 0.0, 500.0, 500.0)
ZONES = [(250.0, 50.0), (250.0, 450.0)]
ZONE_RADIUS = 40.0
# and its servers' settings that the picture depends on
WALLS_LENGTH = 400.0
WALLS_STAY_UP_DELAY = 8.0

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


def heading(sample):
    return (sample["DirX"], sample["DirY"])


def corners(a, b):
    """Points a cycle passed through between two samples that a straight line
    along the first one's heading does not join."""
    ax, ay, bx, by = a["PosX"], a["PosY"], b["PosX"], b["PosY"]
    da, db = heading(a), heading(b)
    along_x = da[0] != 0
    across = abs(by - ay) if along_x else abs(bx - ax)
    if across < 0.06:
        return []
    if da == db:
        # Two turns between samples: step sideways half way along.
        if along_x:
            mid = (ax + bx) / 2
            return [(mid, ay), (mid, by)]
        mid = (ay + by) / 2
        return [(ax, mid), (bx, mid)]
    if da[0] == -db[0] and da[1] == -db[1]:
        # Reversed: one sideways step at the old position.
        return [(ax, by)] if along_x else [(bx, ay)]
    # One turn: leave along the old heading, arrive along the new one.
    return [(bx, ay)] if along_x else [(ax, by)]


def smooth(values, window):
    """Running mean with the window shrinking at the ends."""
    half = window // 2
    total = [0.0]
    for value in values:
        total.append(total[-1] + value)
    out = []
    for index in range(len(values)):
        low, high = max(0, index - half), min(len(values), index + half + 1)
        out.append((total[high] - total[low]) / (high - low))
    return out


def thin(points, keep):
    """Drops points that the straight line (in time and place) between the
    ones kept still passes within KEEP_WITHIN of. keep marks corners."""
    kept = [0]
    start = 0
    for index in range(1, len(points)):
        last = index == len(points) - 1
        if not (keep[index] or last):
            # would every point since the last kept one still be met if this one were dropped?
            t0, x0, y0 = points[start]
            t1, x1, y1 = points[index + 1]
            span = t1 - t0
            fine = span > 1e-6
            for mid in range(start + 1, index + 1):
                if not fine:
                    break
                f = (points[mid][0] - t0) / span
                if (abs(x0 + (x1 - x0) * f - points[mid][1]) > KEEP_WITHIN or
                        abs(y0 + (y1 - y0) * f - points[mid][2]) > KEEP_WITHIN):
                    fine = False
            if fine and span < 2.0:
                continue
        kept.append(index)
        start = index
    return [points[index] for index in kept]


def life_points(samples, death):
    """The life's path as (time, x, y), time rebuilt from the logged speeds."""
    # the path, and where on it each sample lies
    path = [(samples[0]["PosX"], samples[0]["PosY"])]
    corner = [True]
    at = [0]
    for a, b in zip(samples, samples[1:]):
        for point in corners(a, b):
            path.append(point)
            corner.append(True)
        path.append((b["PosX"], b["PosY"]))
        corner.append(heading(a) != heading(b))
        at.append(len(path) - 1)
    if death is not None:
        last = samples[-1]
        gap = abs(death[1] - last["PosX"]) + abs(death[2] - last["PosY"])
        if 0.05 < gap < 40 and 0 <= death[0] - last["ElapsedTime"] < 1.0:
            fake = {"PosX": death[1], "PosY": death[2], "DirX": last["DirX"], "DirY": last["DirY"]}
            for point in corners(last, fake):
                path.append(point)
                corner.append(True)
            path.append((death[1], death[2]))
            corner.append(True)
    corner[-1] = True

    distance = [0.0]
    for p, q in zip(path, path[1:]):
        distance.append(distance[-1] + abs(q[0] - p[0]) + abs(q[1] - p[1]))

    # time at each sample: distance over speed since the sample before
    clock = [s["ElapsedTime"] for s in samples]
    rebuilt = [clock[0]]
    for index in range(1, len(samples)):
        a, b = samples[index - 1], samples[index]
        step = distance[at[index]] - distance[at[index - 1]]
        logged = max(0.0, clock[index] - clock[index - 1])
        speed = 0.5 * (a["Speed"] + b["Speed"])
        if step < 0.05 or speed < SLOW:
            rebuilt.append(rebuilt[-1] + logged)
        else:
            rebuilt.append(rebuilt[-1] + step / speed)
    # held to the log's clock over a couple of seconds, never running backwards
    drift = smooth([c - r for c, r in zip(clock, rebuilt)], CLOCK_WINDOW)
    times = []
    for value, shift in zip(rebuilt, drift):
        t = value + shift
        times.append(t if not times else max(t, times[-1] + 1e-4))

    # time at every point of the path, by distance between the samples around it
    out = []
    sample = 0
    for index, (x, y) in enumerate(path):
        while sample + 1 < len(at) and at[sample + 1] <= index:
            sample += 1
        if at[sample] == index or sample + 1 >= len(at):
            if at[sample] == index:
                t = times[sample]
            else:
                # past the last sample: on at the last speed
                speed = max(samples[-1]["Speed"], 5.0)
                t = times[-1] + (distance[index] - distance[at[-1]]) / speed
        else:
            d0, d1 = distance[at[sample]], distance[at[sample + 1]]
            f = (distance[index] - d0) / (d1 - d0) if d1 > d0 else 0.0
            t = times[sample] + (times[sample + 1] - times[sample]) * f
        if out and t <= out[-1][0]:
            t = out[-1][0] + 1e-4
        out.append((t, x, y))
    return thin(out, corner)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    events = load(sys.argv[1])

    rounds = defaultdict(lambda: {"samples": defaultdict(list), "colour": {},
                                  "team": {}, "end": 0.0, "deaths": defaultdict(list),
                                  "conquests": []})
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
        elif name == "CycleDestroyLog":
            entry["deaths"][event["Username"]].append(
                (event["ElapsedTime"], event["PosX"], event["PosY"]))
        elif name == "ConquerLog":
            entry["conquests"].append((event["ElapsedTime"], event["Team"]))

    players, teams, colours = {}, {}, {}
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
            deaths = sorted(entry["deaths"].get(user, []))
            chunks, life = [], [samples[0]]
            for sample in samples[1:]:
                if sample["ElapsedTime"] - life[-1]["ElapsedTime"] > LIFE_GAP:
                    chunks.append(life)
                    life = []
                life.append(sample)
            chunks.append(life)
            for chunk in chunks:
                if len(chunk) < 2:
                    continue
                end = chunk[-1]["ElapsedTime"]
                death = next((d for d in deaths if -0.3 <= d[0] - end <= 1.0), None)
                points = life_points(chunk, death)
                if len(points) >= 2:
                    lives.append((player, team, points, death is not None))
        lives.sort(key=lambda life: life[2][0][0])
        out_rounds.append((entry["end"], lives, sorted(entry["conquests"])))

    if not out_rounds:
        sys.exit("no playable rounds in input")

    # which zone is whose: the one its riders start around
    names = {index: name.lower() for name, index in teams.items()}
    zone_of = {}
    for team, points in spawns.items():
        y = sum(p[1] for p in points) / len(points)
        zone_of[team] = min(range(len(ZONES)), key=lambda z: abs(ZONES[z][1] - y))

    with open(sys.argv[2], "w", newline="\n") as out:
        out.write("RCLREPLAY 2\n")
        out.write("arena %.1f %.1f %.1f %.1f\n" % ARENA)
        out.write("walls %.1f %.1f\n" % (WALLS_LENGTH, WALLS_STAY_UP_DELAY))
        for team in sorted(zone_of):
            colour = next((rgb for key, rgb in TEAM_COLOURS.items()
                           if key in names[team]),
                          FALLBACK_COLOURS[team % len(FALLBACK_COLOURS)])
            x, y = ZONES[zone_of[team]]
            out.write("zone %d %.1f %.1f %.1f %d %d %d\n" % (team, x, y, ZONE_RADIUS, *colour))
        count = falls = 0
        for end, lives, conquests in out_rounds:
            out.write("round %.2f %d\n" % (end, len(lives)))
            for t, by in conquests:
                # the zone that falls is the other team's
                for team in sorted(zone_of):
                    if team != teams.get(by):
                        out.write("fall %.2f %d\n" % (t, team))
                        falls += 1
            for player, team, points, died in lives:
                red, green, blue = colours.get(player, (15, 15, 15))
                out.write("life %d %d %d %d %d %d %d\n" % (
                    player, team, red, green, blue, 1 if died else 0, len(points)))
                for t, x, y in points:
                    out.write("%.3f %.2f %.2f\n" % (t, x, y))
                count += len(points)
    print("%s: %d rounds, %d players, %d points, %d zone falls" % (
        sys.argv[2], len(out_rounds), len(players), count, falls))


if __name__ == "__main__":
    main()
