#!/usr/bin/env python3
# Afterglow - Checks every station in a stations.txt from a PC: does it send playable audio?
# Copyright (C) 2026 DevvNirvana
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage (Python 3.8 or newer, nothing to install; works on Windows too):
#
#   python tools/check-stations.py                       # assets/radio/stations.txt
#   python tools/check-stations.py my-stations.txt
#   python tools/check-stations.py --write-good good.txt # keep only the ones that work
#
# For each station it connects, follows redirects and .pls/.m3u playlists,
# reads up to 64 KiB within 12 seconds and looks for MP3 or AAC (ADTS) audio
# frames, the formats the app plays. It prints one line per station and a
# summary. The console's own check (Circle > Check stations) is the final
# word: a PC can reach a station the console cannot, and the other way round.

import argparse
import concurrent.futures
import os
import re
import sys
import time
import urllib.error
import urllib.request

READ_BYTES = 64 * 1024
TIMEOUT = 12
HEADERS = {"User-Agent": "Afterglow/1.0 (station check)", "Icy-MetaData": "1", "Accept": "*/*"}


def parse(path):
    stations = []
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split("|")]
            if len(fields) == 6 and re.match(r"https?://", fields[5], re.I):
                stations.append((number, fields, raw.rstrip("\n")))
    return stations


def adts_at(data, i):
    return data[i] == 0xFF and (data[i + 1] & 0xF6) == 0xF0


def mpeg_at(data, i):
    return (data[i] == 0xFF and (data[i + 1] & 0xE0) == 0xE0 and (data[i + 1] & 0x06) != 0
            and (data[i + 2] & 0xF0) != 0xF0 and (data[i + 2] & 0x0C) != 0x0C)


def frames(data, test, length_of):
    """How many frames in a row follow one another, from the best start."""
    best = 0
    for start in range(0, min(len(data) - 8, 8192)):
        if not test(data, start):
            continue
        count, at = 0, start
        while at + 8 < len(data) and test(data, at):
            size = length_of(data, at)
            if size < 7:
                break
            count += 1
            at += size
        best = max(best, count)
        if best >= 4:
            break
    return best


def adts_length(data, i):
    return ((data[i + 3] & 0x03) << 11) | (data[i + 4] << 3) | (data[i + 5] >> 5)


BITRATES = {
    1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],  # MPEG-1 layer III
    2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],  # MPEG-2/2.5 layer III
}
RATES = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


def mp3_length(data, i):
    version = (data[i + 1] >> 3) & 0x03
    layer = (data[i + 1] >> 1) & 0x03
    if layer != 1 or version == 1:  # layer III only (radio), version 01 is reserved
        return 0
    index = data[i + 2] >> 4
    rate_index = (data[i + 2] >> 2) & 0x03
    if index in (0, 15) or rate_index == 3:
        return 0
    bitrate = BITRATES[1 if version == 3 else 2][index] * 1000
    rate = RATES[version][rate_index]
    padding = (data[i + 2] >> 1) & 0x01
    per_frame = 144 if version == 3 else 72
    return per_frame * bitrate // rate + padding


def strip_icy(data, metaint):
    if metaint <= 0:
        return data
    out, at = bytearray(), 0
    while at < len(data):
        out += data[at:at + metaint]
        at += metaint
        if at >= len(data):
            break
        at += 1 + data[at] * 16
    return bytes(out)


def fetch(url, deadline):
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        kind = (response.headers.get("Content-Type") or "").lower()
        metaint = int(response.headers.get("icy-metaint") or 0)
        data = b""
        while len(data) < READ_BYTES and time.monotonic() < deadline:
            chunk = response.read(8192)
            if not chunk:
                break
            data += chunk
        return response.status, kind, metaint, data


def check(url):
    deadline = time.monotonic() + TIMEOUT + 4
    for _ in range(3):  # a playlist may point at a playlist
        try:
            status, kind, metaint, data = fetch(url, deadline)
        except urllib.error.HTTPError as error:
            return False, f"the server answered {error.code}"
        except Exception as error:  # noqa: BLE001 - every failure is a result here
            return False, str(getattr(error, "reason", error))[:80]
        head = data[:4096].decode("latin-1").lstrip()
        if metaint == 0 and (head.lower().startswith(("[playlist]", "#extm3u", "http"))):
            target = None
            for line in head.splitlines():
                line = line.strip()
                if line.lower().startswith("file") and "=" in line:
                    line = line.split("=", 1)[1].strip()
                if re.match(r"https?://", line, re.I):
                    target = line
                    break
            if not target:
                return False, "a playlist without a stream in it"
            url = target
            continue
        if any(word in kind for word in ("ogg", "opus", "flac", "mpegurl", "text/html")):
            return False, f"plays only MP3 and AAC (sends {kind})"
        audio = strip_icy(data, metaint)
        if len(audio) < 4096:
            return False, "too little data arrived"
        aac = frames(audio, adts_at, adts_length)
        mp3 = frames(audio, mpeg_at, mp3_length)
        if aac >= 3:
            return True, f"AAC, {len(audio) // 1024} KiB read"
        if mp3 >= 3:
            return True, f"MP3, {len(audio) // 1024} KiB read"
        return False, f"no MP3 or AAC frames ({kind or 'no type'})"
    return False, "too many playlists"


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description="Check Afterglow radio stations.")
    parser.add_argument("list", nargs="?", default=os.path.join(root, "assets", "radio", "stations.txt"))
    parser.add_argument("--write-good", metavar="FILE", help="write the working stations to FILE")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    stations = parse(args.list)
    print(f"Checking {len(stations)} stations from {args.list} ...\n")
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(check, fields[5]): (number, fields) for number, fields, _ in stations}
        for future in concurrent.futures.as_completed(futures):
            number, fields = futures[future]
            ok, why = future.result()
            results[number] = (ok, why)
            mark = "OK  " if ok else "FAIL"
            print(f"{mark} {fields[0][:14]:14} {fields[1][:34]:34} {why}", flush=True)

    good = [raw for number, _, raw in stations if results[number][0]]
    print(f"\n{len(good)} of {len(stations)} stations sent playable audio.")
    if args.write_good:
        with open(args.write_good, "w", encoding="utf-8") as handle:
            handle.write("# Afterglow stations that passed tools/check-stations.py on "
                         + time.strftime("%Y-%m-%d") + "\n")
            handle.write("\n".join(good) + "\n")
        print(f"Wrote {args.write_good}.")
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
