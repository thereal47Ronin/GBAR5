#!/usr/bin/env python3
# GBAR5 - get a Game Boy Advance library onto the PS5.
# SPDX-License-Identifier: GPL-3.0-or-later
"""gbar5_sync.py - GBA ROMs for the GBAR5 app.

  python tools/gbar5_sync.py prepare --source "D:\\GBA"
  python tools/gbar5_sync.py covers  --stage "D:\\GBAR5_ready"
  python tools/gbar5_sync.py upload  --stage "D:\\GBAR5_ready" --host <console IP>

prepare unpacks every .7z / .zip into one folder per game under the staging
root (GBAR5_ready by default):

  games/<Rom name>/<Rom name>.gba

The ROM keeps its No-Intro name, which is what libretro-thumbnails calls the
box art. covers saves that art as cover.png beside the ROM, which the app
picks up without any name matching. upload mirrors the tree to the console
over FTP and writes /data/GBAR5/library.txt.

No ROMs are included with GBAR5. Use dumps of cartridges you own.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import ftplib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from pathlib import Path

REMOTE_ROOT = "/data/GBAR5"
COVER_URL = ("https://raw.githubusercontent.com/libretro-thumbnails/"
             "Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/{name}.png")
ROM_EXTS = {".gba", ".agb"}
BAD = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


def seven_zip() -> str:
    for c in ("7z", "7za", r"C:\Program Files\7-Zip\7z.exe",
              r"C:\Program Files (x86)\7-Zip\7z.exe"):
        if os.path.sep in c and not Path(c).is_file():
            continue
        return c
    raise SystemExit("7-Zip not found: install it, or put 7z on PATH")


def safe(name: str, limit: int = 92) -> str:
    name = BAD.sub("_", name).strip().rstrip(". ")
    if len(name) > limit:
        # No-Intro names can be 120+ characters; with the folder prefix that
        # blows past MAX_PATH. Keep the front, which is the game's name.
        name = name[:limit].rstrip(". ")
    return name or "game"


def region_of(name: str) -> str:
    if "(USA" in name or "(World" in name or "(U)" in name:
        return "U"
    if "(Europe" in name or "(E)" in name:
        return "E"
    if "(Japan" in name or "(J)" in name:
        return "J"
    return ""


def staging_root(path: str | None) -> Path:
    return Path(path) if path else Path("GBAR5_ready")


def extract_one(archive: Path, out: Path) -> Path:
    with tempfile.TemporaryDirectory(dir=str(out)) as tmp:
        subprocess.run([seven_zip(), "x", "-y", f"-o{tmp}", str(archive)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        found = [p for p in Path(tmp).rglob("*") if p.suffix.lower() in ROM_EXTS]
        if not found:
            raise SystemExit(f"no ROM inside {archive.name}")
        # the biggest file wins when an archive holds several regional dumps
        rom = max(found, key=lambda p: p.stat().st_size)
        name = rom.stem
        folder = out / "games" / safe(name)
        folder.mkdir(parents=True, exist_ok=True)
        dest = folder / f"{safe(name)}{rom.suffix.lower()}"
        if dest.exists() and dest.stat().st_size == rom.stat().st_size:
            return dest
        shutil.move(str(rom), str(dest))
        return dest


def cmd_prepare(args) -> None:
    src = Path(args.source)
    stage = staging_root(args.stage)
    (stage / "games").mkdir(parents=True, exist_ok=True)
    archives = sorted(p for p in src.iterdir() if p.suffix.lower() in (".7z", ".zip"))
    if args.limit:
        archives = archives[: args.limit]
    print(f"{len(archives)} archives -> {stage / 'games'}")
    done = failed = 0
    for i, arc in enumerate(archives, 1):
        try:
            dest = extract_one(arc, stage)
            if dest.exists():
                done += 1
                print(f"[{i}/{len(archives)}] {dest.parent.name}")
        except SystemExit as exc:
            failed += 1
            print(f"[{i}/{len(archives)}] FAILED {arc.name}: {exc}")
        except subprocess.CalledProcessError:
            failed += 1
            print(f"[{i}/{len(archives)}] FAILED {arc.name}: 7z error")
    print(f"prepared {done}, failed {failed}")


def fetch_cover(name: str, dest: Path) -> bool:
    if dest.is_file():
        return True
    url = COVER_URL.format(name=urllib.parse.quote(name))
    req = urllib.request.Request(url, headers={"User-Agent": "GBAR5-sync/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=25) as r, open(dest, "wb") as f:
            shutil.copyfileobj(r, f)
        return dest.stat().st_size > 1000
    except Exception:
        if dest.exists():
            dest.unlink()
        return False


def cmd_covers(args) -> None:
    stage = staging_root(args.stage)
    games = stage / "games"
    jobs = []
    for folder in sorted(p for p in games.iterdir() if p.is_dir()):
        rom = next((p for p in folder.iterdir() if p.suffix.lower() in ROM_EXTS), None)
        if rom is None:
            continue
        jobs.append((rom.stem, folder / "cover.png"))
    if args.limit:
        jobs = jobs[: args.limit]
    print(f"{len(jobs)} covers to check")
    ok = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(fetch_cover, n, d): n for n, d in jobs}
        for i, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            if fut.result():
                ok += 1
            elif i % 25 == 0 or i == len(jobs):
                print(f"[{i}/{len(jobs)}] {ok} found")
    print(f"covers: {ok} present, {len(jobs) - ok} missing")


def ftp_connect(args) -> ftplib.FTP:
    f = ftplib.FTP()
    f.connect(args.host, args.port, timeout=30)
    f.login(args.user, args.password)
    f.set_pasv(True)
    return f


def ensure_dir(f: ftplib.FTP, path: str) -> None:
    parts = [p for p in path.split("/") if p]
    cur = ""
    for p in parts:
        cur += "/" + p
        try:
            f.mkd(cur)
        except ftplib.all_errors:
            pass


def remote_size(f: ftplib.FTP, path: str) -> int | None:
    try:
        return f.size(path)
    except ftplib.all_errors:
        return None


def cmd_upload(args) -> None:
    stage = staging_root(args.stage)
    if not (stage / "games").is_dir():
        raise SystemExit(f"nothing staged in {stage}; run prepare first")
    files = [p for p in stage.rglob("*") if p.is_file() and p.name != "library.txt"]
    if args.limit:
        files = files[: args.limit]
    total = sum(p.stat().st_size for p in files)
    print(f"{len(files)} files, {total / 1e9:.2f} GB -> {args.host}:{REMOTE_ROOT}")

    def one(f, path, chunk_no):
        rel = path.relative_to(stage).as_posix()
        remote = f"{REMOTE_ROOT}/{rel}"
        size = path.stat().st_size
        if remote_size(f, remote) == size:
            return 0
        ensure_dir(f, remote.rsplit("/", 1)[0])
        try:
            f.delete(remote)
        except ftplib.all_errors:
            pass
        with open(path, "rb") as fh:
            f.storbinary(f"STOR {remote}", fh, blocksize=256 * 1024)
        print(f"  [w{chunk_no}] {rel} {size / 1e6:.1f} MB", flush=True)
        return size

    def upload_range(chunk, chunk_no):
        f = ftp_connect(args)
        sent = 0
        for path in chunk:
            for attempt in range(4):
                try:
                    sent += one(f, path, chunk_no)
                    break
                except (ftplib.all_errors + (OSError,)) as exc:
                    if attempt == 3:
                        print(f"  [w{chunk_no}] GIVE UP {path.name}: {exc}", flush=True)
                        break
                    try:
                        f.close()
                    except Exception:
                        pass
                    time.sleep(2 + attempt * 3)
                    try:
                        f = ftp_connect(args)
                    except Exception as exc2:
                        print(f"  [w{chunk_no}] reconnect failed: {exc2}", flush=True)
                        time.sleep(5)
        f.close()
        return sent

    workers = max(1, args.workers)
    chunks = [files[i::workers] for i in range(workers)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(upload_range, c, i + 1) for i, c in enumerate(chunks)]
        sent = sum(f.result() for f in futures)
    print(f"uploaded {sent / 1e9:.2f} GB")

    # the index, for a sandboxed app that can't list /data
    lines = ["# title\tserial\tdiscs\tpath\tdisc name"]
    for folder in sorted(p for p in (stage / "games").iterdir() if p.is_dir()):
        rom = next((p for p in folder.iterdir() if p.suffix.lower() in ROM_EXTS), None)
        if rom is None:
            continue
        lines.append("\t".join([
            rom.stem, region_of(rom.stem), "1",
            f"{REMOTE_ROOT}/games/{folder.name}/{rom.name}", rom.stem,
        ]))
    index = stage / "library.txt"
    index.write_text("\n".join(lines) + "\n", encoding="utf-8")
    f = ftp_connect(args)
    ensure_dir(f, REMOTE_ROOT)
    with open(index, "rb") as fh:
        f.storbinary(f"STOR {REMOTE_ROOT}/library.txt", fh)
    f.quit()
    print(f"index: {len(lines) - 1} games")


def main() -> int:
    ap = argparse.ArgumentParser(description="GBAR5 game library sync")
    ap.add_argument("command", choices=["prepare", "covers", "upload"])
    ap.add_argument("--source", default=".", help="folder of .7z / .zip archives")
    ap.add_argument("--stage", default=None, help="staging folder (default GBAR5_ready)")
    ap.add_argument("--host", required=True, help="the console's IP address")
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--user", default="anonymous")
    ap.add_argument("--password", default="codex")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    {"prepare": cmd_prepare, "covers": cmd_covers, "upload": cmd_upload}[args.command](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
