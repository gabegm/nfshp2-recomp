#!/usr/bin/env python3
"""One-time RetroAchievements login; stores the API token outside Git."""
import getpass
import json
import os
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: ra-login.py CREDENTIALS_FILE", file=sys.stderr)
        return 2
    if not sys.stdin.isatty():
        print("Run this command in a terminal so the password can be entered privately.", file=sys.stderr)
        return 2
    username = input("RetroAchievements username: ").strip()
    if not username or "\n" in username:
        print("A username is required.", file=sys.stderr)
        return 2
    password = getpass.getpass("RetroAchievements password: ")
    if not password:
        print("A password is required.", file=sys.stderr)
        return 2
    body = urllib.parse.urlencode({"r": "login2", "u": username, "p": password}).encode()
    request = urllib.request.Request(
        "https://retroachievements.org/dorequest.php",
        data=body,
        headers={"User-Agent": "nfshp2-recomp/0.1 (Linux) rcheevos/12.4.0"},
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            result = json.load(response)
    except (OSError, urllib.error.URLError, ValueError) as error:
        print(f"RetroAchievements login request failed: {error}", file=sys.stderr)
        return 1
    if not isinstance(result, dict) or not result.get("Success") or not result.get("Token"):
        print(f"RetroAchievements login failed: {result.get('Error', 'unknown error')}", file=sys.stderr)
        return 1
    name, token = result.get("User", username), result["Token"]
    if not isinstance(name, str) or not isinstance(token, str) or "\n" in name or "\n" in token:
        print("RetroAchievements returned invalid credentials.", file=sys.stderr)
        return 1
    path = Path(sys.argv[1])
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    with os.fdopen(fd, "w") as output:
        os.fchmod(output.fileno(), 0o600)
        output.write(f"{name}\n{token}\n")
    print(f"Signed in as {name}. Future runs of play.sh will enable casual achievements.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
