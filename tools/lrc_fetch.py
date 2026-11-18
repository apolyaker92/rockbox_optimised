#!/usr/bin/env python3
"""Fetch synced lyrics (.lrc) for the tracks in a music library from LRCLIB.

usage: lrc_fetch.py MUSIC_ROOT [--apply] [--limit N] [--shuffle]
                    [--delay SECONDS] [--cache FILE] [--retry-days N]

By default this is a dry run: it looks everything up and reports what it
would write, but writes nothing. Add --apply to write the files.

For every audio file that has no .lrc next to it, it reads the artist, title,
album and length from the tags and asks https://lrclib.net for the lyrics
(that is what is sent: those four things, and nothing else). A result is
only used if it has line time stamps. It never overwrites an existing .lrc.
Instrumentals, plain (unsynced) lyrics and misses are remembered in a cache
file so the next run does not ask again for a while (--retry-days).

Needs mutagen (pip install mutagen). The rest is the standard library.
"""
import argparse
import json
import os
import random
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

try:
    import mutagen
except ImportError:
    sys.exit("needs mutagen: pip install mutagen")

API = "https://lrclib.net/api"
AUDIO = {".flac", ".mp3", ".m4a", ".ogg", ".opus", ".wav", ".wv", ".ape",
         ".mpc", ".aac", ".wma"}
STAMP = re.compile(r"^\s*\[\d{1,3}:\d{2}(?:[.:]\d{1,3})?\]", re.M)
USER_AGENT = "lrc-fetch/0.1"


class Limited(Exception):
    pass


def get_json(path, params, delay):
    """GET and decode; None for 404. Honours Retry-After on 429/503."""
    url = f"{API}{path}?{urllib.parse.urlencode(params)}"
    for attempt in range(4):
        time.sleep(delay)
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(req, timeout=25) as resp:
                return json.load(resp)
        except urllib.error.HTTPError as err:
            if err.code == 404:
                return None
            if err.code in (429, 503):
                wait = float(err.headers.get("Retry-After") or 5)
                time.sleep(wait * (1 + random.random() * 0.3))
                continue
            if err.code == 400:
                return None
            raise
        except (urllib.error.URLError, TimeoutError):
            time.sleep(3 * (attempt + 1))
    raise Limited("too many failures talking to lrclib.net")


def first(tags, *keys):
    for key in keys:
        values = tags.get(key) if tags else None
        if values:
            return str(values[0]).strip()
    return ""


def describe(path):
    """(artist, title, album, seconds) from the tags, with the file name as
    a fallback for the title."""
    audio = mutagen.File(path, easy=True)
    tags = audio if audio is not None else {}
    artist = first(tags, "artist", "albumartist")
    title = first(tags, "title")
    album = first(tags, "album")
    seconds = int(round(audio.info.length)) if audio is not None else 0
    if not title:
        title = re.sub(r"^\s*\d+\s*[-.]\s*", "",
                       os.path.splitext(os.path.basename(path))[0])
    return artist, title, album, seconds


def title_variants(title):
    """The title as is, then without trailing (Explicit) / [Remastered] bits."""
    out = [title]
    plain = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]\s*$", "", title).strip()
    plain = re.sub(r"\s+-\s+(remaster(ed)?|live|mono|stereo).*$", "", plain,
                   flags=re.I)
    if plain and plain != title:
        out.append(plain)
    return out


def artist_variants(artist):
    """The credit as tagged, then just the first artist ("A, B & C" -> "A")."""
    out = [artist]
    first_artist = re.split(r"\s*(?:,|;|&| feat\.? | ft\.? | x | and )\s*",
                            artist, maxsplit=1, flags=re.I)[0].strip()
    if first_artist and first_artist != artist:
        out.append(first_artist)
    return out


def usable(rec, seconds):
    synced = (rec or {}).get("syncedLyrics") or ""
    if len(STAMP.findall(synced)) < 3:
        return False
    return not seconds or abs((rec.get("duration") or 0) - seconds) <= 3


def lookup(artist, title, album, seconds, delay):
    """-> (status, lrc_text). status: found, instrumental, plain, miss."""
    saw_plain = False
    for who in artist_variants(artist):
        for name in title_variants(title):
            for with_album in (True, False):
                params = {"artist_name": who, "track_name": name}
                if with_album and album:
                    params["album_name"] = album
                if seconds:
                    params["duration"] = seconds
                rec = get_json("/get", params, delay)
                if not rec:
                    continue
                if usable(rec, seconds):
                    return "found", rec["syncedLyrics"]
                if rec.get("instrumental"):
                    return "instrumental", None
                saw_plain = saw_plain or bool(rec.get("plainLyrics"))
            # search is looser: take the closest-length record with stamps
            results = get_json("/search", {"track_name": name,
                                           "artist_name": who}, delay) or []
            results = [r for r in results if usable(r, seconds)]
            if results:
                best = min(results, key=lambda r: abs((r.get("duration") or 0)
                                                      - seconds))
                return "found", best["syncedLyrics"]
    return ("plain" if saw_plain else "miss"), None


def main():
    # titles can be Japanese etc.; never die on a console that cannot print them
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("root")
    ap.add_argument("--apply", action="store_true",
                    help="write the .lrc files (default: dry run)")
    ap.add_argument("--limit", type=int, default=0,
                    help="look up at most this many tracks")
    ap.add_argument("--shuffle", action="store_true",
                    help="visit tracks in random order (good for a sample)")
    ap.add_argument("--seed", type=int, help="make --shuffle repeatable")
    ap.add_argument("--delay", type=float, default=0.7,
                    help="seconds between requests (default 0.7)")
    ap.add_argument("--cache", default=os.path.expanduser("~/.lrc_fetch_cache.json"))
    ap.add_argument("--retry-days", type=float, default=30,
                    help="ask again about misses after this many days")
    args = ap.parse_args()

    cache = {}
    if os.path.exists(args.cache):
        cache = json.load(open(args.cache, encoding="utf-8"))
    now = time.time()

    todo = []
    for folder, dirs, names in os.walk(args.root):
        dirs[:] = [d for d in dirs if d not in ("System Volume Information", ".rockbox")]
        stems = {os.path.splitext(n)[0] for n in names
                 if n.lower().endswith(".lrc")}
        for n in names:
            stem, ext = os.path.splitext(n)
            if ext.lower() in AUDIO and stem not in stems:
                todo.append(os.path.join(folder, n))
    skipped = 0
    fresh = []
    for path in todo:
        hit = cache.get(os.path.relpath(path, args.root))
        if hit and now - hit["ts"] < args.retry_days * 86400:
            skipped += 1
        else:
            fresh.append(path)
    if args.shuffle:
        random.Random(args.seed).shuffle(fresh)
    if args.limit:
        fresh = fresh[:args.limit]
    print(f"{len(todo)} tracks have no .lrc; {skipped} asked about recently; "
          f"looking up {len(fresh)}{'' if args.apply else ' (dry run)'}")

    counts = {"found": 0, "instrumental": 0, "plain": 0, "miss": 0}
    try:
        for i, path in enumerate(fresh, 1):
            artist, title, album, seconds = describe(path)
            label = f"{artist} - {title}"
            if not artist or not title:
                status, text = "miss", None
            else:
                status, text = lookup(artist, title, album, seconds, args.delay)
            counts[status] += 1
            print(f"[{i}/{len(fresh)}] {status.upper():12} {label}")
            if status == "found" and args.apply:
                out = os.path.splitext(path)[0] + ".lrc"
                if not os.path.exists(out):
                    with open(out, "w", encoding="utf-8", newline="\n") as f:
                        f.write(text.rstrip("\n") + "\n")
            if status != "found" and args.apply:
                cache[os.path.relpath(path, args.root)] = {"status": status, "ts": now}
            if args.apply and i % 50 == 0:      # survive a reboot mid-run
                json.dump(cache, open(args.cache, "w", encoding="utf-8"))
    except (Limited, KeyboardInterrupt) as err:
        print(f"stopped: {err or 'interrupted'}")
    finally:
        if args.apply:
            json.dump(cache, open(args.cache, "w", encoding="utf-8"))
    total = sum(counts.values()) or 1
    print("\n" + ", ".join(f"{k} {v}" for k, v in counts.items())
          + f"  ({100 * counts['found'] // total}% synced)")


if __name__ == "__main__":
    main()
