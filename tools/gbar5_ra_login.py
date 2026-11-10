#!/usr/bin/env python3
# GBAR5 - sign the console in to RetroAchievements from a PC.
# SPDX-License-Identifier: GPL-3.0-or-later
"""gbar5_ra_login.py - put a RetroAchievements sign-in on a GBAR5 console.

The console never sees the password. This asks retroachievements.org for a
login token, then writes that token to /data/GBAR5/retroachievements.ini over
FTP. GBAR5 reads it the next time it starts (or when you switch profiles).

  python tools/gbar5_ra_login.py --host <console IP>
  python tools/gbar5_ra_login.py --host http://<console IP>:8095/   (the address on the TV)
  python tools/gbar5_ra_login.py --host <console IP> --user your-username
  python tools/gbar5_ra_login.py --host <console IP> --hardcore
  python tools/gbar5_ra_login.py --host <console IP> --profile "Player 2"

Signing out is done on the console itself: Settings > System > Settings from
your phone, on a phone, or by signing in again with --profile.
"""
from __future__ import annotations

import argparse
import ftplib
import getpass
import io
import json
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REMOTE_ROOT = "/data/GBAR5"
RA_URL = "https://retroachievements.org/dorequest.php"


def host_only(value: str) -> str:
    """'http://<console IP>:8095/?t=...' (the card's address) or a bare IP."""
    value = value.strip()
    if "://" in value:
        value = urllib.parse.urlsplit(value).hostname or value
    value = value.split("/")[0]
    # the card's address carries the phone page's port (8095), not FTP's (2121)
    return value.split(":")[0]


def profile_folder(name: str) -> str:
    """The same rule as src/profiles.c: letters, digits, spaces, - and _."""
    return "".join(c if (c.isascii() and c.isalnum()) or c in " -_" else "_" for c in name)


def fetch_token(user: str, password: str) -> tuple[str, str]:
    data = urllib.parse.urlencode({"r": "login2", "u": user, "p": password}).encode()
    del password
    request = urllib.request.Request(
        RA_URL, data=data, headers={"User-Agent": "GBAR5/1.0 (PC sign-in)"}
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as reply:
            body = json.load(reply)
    except urllib.error.HTTPError as error:
        body = json.load(error) if error.headers.get_content_type() == "application/json" else {}
    except urllib.error.URLError as error:
        sys.exit(f"couldn't reach retroachievements.org: {error.reason}")
    if not body.get("Success") or not body.get("Token"):
        sys.exit(f"sign-in failed: {body.get('Error') or 'unknown error'}")
    return body.get("User") or user, body["Token"]


def upload(host: str, port: int, root: str, profile: str | None, ini: bytes) -> str:
    remote_dir = root.rstrip("/")
    if profile:
        remote_dir += "/profiles/" + profile_folder(profile)
    remote = remote_dir + "/retroachievements.ini"
    ftp = ftplib.FTP()
    try:
        ftp.connect(host, port, timeout=30)
        ftp.login()
        ftp.set_pasv(True)
        ftp.voidcmd("TYPE I")
        try:
            ftp.storbinary("STOR " + remote, io.BytesIO(ini))
        except ftplib.error_perm:
            # first sign-in for this profile: its folder may not exist yet
            make_dirs(ftp, remote_dir)
            ftp.storbinary("STOR " + remote, io.BytesIO(ini))
        ftp.quit()
    except OSError as error:
        sys.exit(f"couldn't reach the console at {host}:{port}: {error}")
    return remote


def make_dirs(ftp: ftplib.FTP, path: str) -> None:
    current = ""
    for part in path.strip("/").split("/"):
        current += "/" + part
        try:
            ftp.mkd(current)
        except ftplib.all_errors:
            pass  # already there


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Sign a GBAR5 console in to RetroAchievements (token only)."
    )
    parser.add_argument("--host", required=True,
                        help="the console: its IP, or the address shown on the TV")
    parser.add_argument("--port", type=int, default=2121, help="etaHEN FTP port (default 2121)")
    parser.add_argument("--user", help="RetroAchievements user name (asked if omitted)")
    parser.add_argument("--hardcore", action="store_true", help="start in hardcore mode")
    parser.add_argument("--profile", help="a GBAR5 profile's name (the main profile if omitted)")
    parser.add_argument("--password-stdin", action="store_true",
                        help="read the password from stdin, for scripts (it is never stored)")
    parser.add_argument("--root", default=REMOTE_ROOT,
                        help=f"GBAR5's data folder on the console (default {REMOTE_ROOT})")
    args = parser.parse_args()

    host = host_only(args.host)
    user = args.user or input("RetroAchievements user name: ").strip()
    if not user:
        sys.exit("a RetroAchievements user name is needed")
    if args.password_stdin:
        password = sys.stdin.readline().rstrip("\r\n")
    else:
        password = getpass.getpass("RetroAchievements password (not stored): ")
    name, token = fetch_token(user, password)
    del password

    ini = (
        "# RetroAchievements sign-in (token only, never the password)\n"
        f"user={name}\ntoken={token}\nhardcore={int(args.hardcore)}\n"
    ).encode("utf-8")
    remote = upload(host, args.port, args.root, args.profile, ini)

    who = f" as {name}"
    if args.profile:
        who += f" for the profile {args.profile}"
    print(f"Signed in{who}.")
    print(f"Wrote {remote} ({len(ini)} bytes).")
    print("Restart GBAR5 (close and open it, or switch profiles) to use it.")


if __name__ == "__main__":
    main()
