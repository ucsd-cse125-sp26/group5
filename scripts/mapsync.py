#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import shlex
import socket
import subprocess
import sys
import textwrap
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = REPO_ROOT / ".mapsync.local.json"
LOCAL_MAPS_DIR = REPO_ROOT / "maps"

DEFAULT_HOST = "cse125.ucsd.edu"
DEFAULT_PORT = 222
DEFAULT_REMOTE_ROOT = "/var/www/html/cse125/2026/cse125g5/maps"

MAP_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.\-]{0,63}$")


def die(msg: str, code: int = 1) -> None:
    print(f"mapsync: {msg}", file=sys.stderr)
    sys.exit(code)


def validate_map_name(name: str) -> str:
    if not MAP_NAME_RE.match(name):
        die(
            f"invalid map name {name!r}: must match {MAP_NAME_RE.pattern} "
            "(alphanumerics, underscore, dot, hyphen; <=64 chars)"
        )
    return name


def load_state() -> dict:
    if CONFIG_PATH.exists():
        with CONFIG_PATH.open("r", encoding="utf-8") as fh:
            return json.load(fh)
    return {}


def save_state(state: dict) -> None:
    tmp = CONFIG_PATH.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8") as fh:
        json.dump(state, fh, indent=2, sort_keys=True)
        fh.write("\n")
    tmp.replace(CONFIG_PATH)


def ensure_config(state: dict) -> dict:
    changed = False
    env_user = os.environ.get("CSE125_USER")
    if env_user:
        state["server_user"] = env_user
        changed = True
    if not state.get("server_user"):
        try:
            answer = input("CSE125 server username: ").strip()
        except EOFError:
            answer = ""
        if not answer:
            die("server_user is required (set CSE125_USER or answer the prompt)")
        state["server_user"] = answer
        changed = True
    state.setdefault("server_host", DEFAULT_HOST)
    state.setdefault("server_port", DEFAULT_PORT)
    state.setdefault("server_root", DEFAULT_REMOTE_ROOT)
    state.setdefault("checkouts", {})
    if changed or not CONFIG_PATH.exists():
        save_state(state)
    return state


def ssh_target(state: dict) -> str:
    return f"{state['server_user']}@{state['server_host']}"


def ssh_base_args(state: dict) -> list[str]:
    return ["ssh", "-p", str(state["server_port"]), ssh_target(state)]


def rsync_base_args(state: dict) -> list[str]:
    # rsync writes to a hidden temp file and atomic-renames; partial
    # transfers never replace the existing file.
    return ["rsync", "-q", "-e", f"ssh -p {state['server_port']}"]


def remote_path(state: dict, *parts: str) -> str:
    return "/".join([state["server_root"].rstrip("/"), *parts])


# Avoid str.format: shell templates contain literal { and }.
def render_script(template: str, **subs: str) -> str:
    out = template
    for key, value in subs.items():
        out = out.replace(f"__{key.upper()}__", value)
    return out


def ssh_run(
    state: dict, script: str, *, check: bool = True
) -> subprocess.CompletedProcess:
    args = [*ssh_base_args(state), "bash", "-s"]
    proc = subprocess.run(
        args, input=script.encode("utf-8"), capture_output=True, check=False
    )
    if check and proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die(f"remote command failed (exit {proc.returncode})")
    return proc


def rsync_up(state: dict, local: Path, remote: str) -> None:
    args = [*rsync_base_args(state), str(local), f"{ssh_target(state)}:{remote}"]
    proc = subprocess.run(args, capture_output=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die(f"rsync upload failed (exit {proc.returncode})")


def rsync_down(state: dict, remote: str, local: Path) -> None:
    args = [*rsync_base_args(state), f"{ssh_target(state)}:{remote}", str(local)]
    proc = subprocess.run(args, capture_output=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die(f"rsync download failed (exit {proc.returncode})")


def now_iso() -> str:
    return _dt.datetime.now(tz=_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def lock_metadata(state: dict, description: str, epoch: int,
                  lock_uuid: str) -> dict:
    return {
        "user": state["server_user"],
        "host": socket.gethostname(),
        "description": description,
        "epoch_at_checkout": epoch,
        "timestamp": now_iso(),
        "lock_uuid": lock_uuid,
    }


def fmt_lock(meta: dict) -> str:
    if not meta:
        return "(no metadata)"
    return (
        f"holder={meta.get('user', '?')} "
        f"host={meta.get('host', '?')} "
        f"epoch={meta.get('epoch_at_checkout', '?')} "
        f"at={meta.get('timestamp', '?')} "
        f"reason={meta.get('description', '')!r}"
    )


REMOTE_LIST_SCRIPT = r"""
set -u
ROOT=__ROOT__
mkdir -p "$ROOT"
chmod 2770 "$ROOT" 2>/dev/null || true
cd "$ROOT"
for d in */; do
  [ -d "$d" ] || continue
  name="${d%/}"
  epoch="?"
  [ -f "$name/epoch" ] && epoch="$(cat "$name/epoch")"
  if [ -d "$name/LOCK" ]; then
    if [ -f "$name/LOCK/metadata" ]; then
      meta="$(cat "$name/LOCK/metadata")"
    else
      meta="{}"
    fi
    printf 'MAP\t%s\t%s\tLOCKED\t%s\n' "$name" "$epoch" "$meta"
  else
    printf 'MAP\t%s\t%s\tFREE\t{}\n' "$name" "$epoch"
  fi
done
"""


def cmd_list(args: argparse.Namespace, state: dict) -> int:
    script = render_script(REMOTE_LIST_SCRIPT, root=shlex.quote(state["server_root"]))
    proc = ssh_run(state, script)
    rows = []
    for line in proc.stdout.decode("utf-8", errors="replace").splitlines():
        if not line.startswith("MAP\t"):
            continue
        _, name, epoch, status, meta_json = line.split("\t", 4)
        meta = {}
        try:
            meta = json.loads(meta_json) if meta_json else {}
        except json.JSONDecodeError:
            pass
        rows.append((name, epoch, status, meta))
    if not rows:
        print("(no maps on server)")
        return 0
    width = max(len(r[0]) for r in rows)
    for name, epoch, status, meta in rows:
        suffix = ""
        if status == "LOCKED":
            suffix = "  " + fmt_lock(meta)
        print(f"{name.ljust(width)}  epoch={epoch}  {status}{suffix}")
    return 0


def cmd_status(args: argparse.Namespace, state: dict) -> int:
    checkouts = state.get("checkouts", {})
    if not checkouts:
        print("(no local checkouts)")
        return 0
    width = max(len(n) for n in checkouts)
    for name in sorted(checkouts):
        info = checkouts[name]
        print(
            f"{name.ljust(width)}  epoch={info['epoch_at_checkout']}  "
            f"since={info['checked_out_at']}  reason={info['description']!r}"
        )
    return 0


REMOTE_NEW_SCRIPT = r"""
set -eu
ROOT=__ROOT__
NAME=__NAME__
TS=__TS__
mkdir -p "$ROOT"
chmod 2770 "$ROOT" 2>/dev/null || true
DIR="$ROOT/$NAME"
# mkdir as the atomic claim; [ -e ] + mkdir races on concurrent 'new'.
if ! mkdir "$DIR" 2>/dev/null; then
  if [ -e "$DIR" ]; then
    echo "EXISTS" >&2
    exit 2
  fi
  echo "MKDIR_FAIL" >&2
  exit 11
fi
mkdir "$DIR/history"
echo 0 > "$DIR/epoch"
GROUP="$(stat -c %g "$ROOT" 2>/dev/null || stat -f %g "$ROOT")"
chgrp -R "$GROUP" "$DIR" 2>/dev/null || true
chmod -R g+rwsX "$DIR" 2>/dev/null || true
chmod 2770 "$DIR" 2>/dev/null || true
echo "READY $TS"
"""


def cmd_new(args: argparse.Namespace, state: dict) -> int:
    name = validate_map_name(args.name)
    local = LOCAL_MAPS_DIR / f"{name}.blend"
    if not local.is_file():
        die(f"expected {local.relative_to(REPO_ROOT)} to exist")
    if not args.message:
        die("'-m <description>' is required")

    script = render_script(
        REMOTE_NEW_SCRIPT,
        root=shlex.quote(state["server_root"]),
        name=shlex.quote(name),
        ts=shlex.quote(now_iso()),
    )
    proc = ssh_run(state, script, check=False)
    if proc.returncode == 2:
        die(f"map {name!r} already exists on server (use 'checkout' instead)")
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die(f"remote setup failed (exit {proc.returncode})")

    incoming = remote_path(state, name, ".incoming.blend")
    rsync_up(state, local, incoming)

    finalize = textwrap.dedent(rf"""
        set -eu
        DIR={shlex.quote(remote_path(state, name))}
        mv "$DIR/.incoming.blend" "$DIR/current.blend"
        echo 1 > "$DIR/epoch"
        chmod -R g+rwsX "$DIR"
        chmod 2770 "$DIR" "$DIR/history"
    """).strip()
    ssh_run(state, finalize)

    print(f"created map {name!r} at epoch 1")
    print("note: 'new' does not check the map out — use 'checkout' to start editing")
    return 0


REMOTE_CHECKOUT_SCRIPT = r"""
set -u
ROOT=__ROOT__
NAME=__NAME__
META=__META__
DIR="$ROOT/$NAME"
if [ ! -d "$DIR" ]; then
  echo "NOMAP" >&2
  exit 3
fi
EPOCH="$(cat "$DIR/epoch" 2>/dev/null || echo '?')"
# Stage metadata in place, then mv -T to acquire — mv -T fails if the
# target exists, giving us a mutex with no metadata-less window.
STAGING="$DIR/.LOCK.staging.$$"
trap 'rm -rf "$STAGING"' EXIT
mkdir "$STAGING" || { echo "STAGE_FAIL" >&2; exit 6; }
printf '%s' "$META" > "$STAGING/metadata"
chmod 2770 "$STAGING"
if ! mv -T "$STAGING" "$DIR/LOCK" 2>/dev/null; then
  echo "LOCKED $EPOCH"
  if [ -f "$DIR/LOCK/metadata" ]; then
    cat "$DIR/LOCK/metadata"
  fi
  exit 4
fi
trap - EXIT
echo "ACQUIRED $EPOCH"
"""


def cmd_checkout(args: argparse.Namespace, state: dict) -> int:
    name = validate_map_name(args.name)
    if not args.message:
        die("'-m <description>' is required")
    if name in state.get("checkouts", {}):
        die(
            f"already have a local checkout of {name!r} "
            f"(epoch {state['checkouts'][name]['epoch_at_checkout']}); "
            "run 'checkin' or 'abandon' first"
        )

    LOCAL_MAPS_DIR.mkdir(exist_ok=True)
    local_blend = LOCAL_MAPS_DIR / f"{name}.blend"

    # Metadata lands atomically with LOCK; epoch patched after since the
    # server picks it.
    lock_uuid = str(uuid.uuid4())
    meta_pending = lock_metadata(state, args.message, -1, lock_uuid)
    meta_json = json.dumps(meta_pending, sort_keys=True)

    script = render_script(
        REMOTE_CHECKOUT_SCRIPT,
        root=shlex.quote(state["server_root"]),
        name=shlex.quote(name),
        meta=shlex.quote(meta_json),
    )
    proc = ssh_run(state, script, check=False)
    out = proc.stdout.decode("utf-8", errors="replace").strip()
    err = proc.stderr.decode("utf-8", errors="replace").strip()
    if proc.returncode == 3:
        die(f"map {name!r} does not exist on server (use 'new' to create it)")
    if proc.returncode == 4:
        first, _, rest = out.partition("\n")
        epoch = first.split(" ", 1)[1] if " " in first else "?"
        try:
            meta = json.loads(rest) if rest.strip() else {}
        except json.JSONDecodeError:
            meta = {}
        die(
            f"map {name!r} is checked out by someone else (epoch {epoch})\n"
            f"  {fmt_lock(meta)}"
        )
    if proc.returncode != 0:
        sys.stderr.write(err)
        die(f"remote checkout failed (exit {proc.returncode})")

    if not out.startswith("ACQUIRED "):
        die(f"unexpected server response: {out!r}")

    # Lock is held on the server — every failure path below must release
    # it, including SystemExit and KeyboardInterrupt, hence BaseException.
    release = textwrap.dedent(rf"""
        set -u
        DIR={shlex.quote(remote_path(state, name))}
        UUID={shlex.quote(lock_uuid)}
        HOLDER_UUID=""
        if [ -f "$DIR/LOCK/metadata" ]; then
          HOLDER_UUID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("lock_uuid",""))' "$DIR/LOCK/metadata" 2>/dev/null || true)"
        fi
        if [ "$HOLDER_UUID" = "$UUID" ]; then
          rm -rf "$DIR/LOCK"
        fi
    """).strip()

    try:
        try:
            epoch = int(out.split(" ", 1)[1])
        except ValueError:
            die(f"server returned non-integer epoch: {out!r}")

        final_meta = lock_metadata(state, args.message, epoch, lock_uuid)
        final_meta_json = json.dumps(final_meta, sort_keys=True)
        update_meta = textwrap.dedent(rf"""
            set -eu
            DIR={shlex.quote(remote_path(state, name))}
            TMP="$DIR/LOCK/.metadata.tmp"
            printf '%s' {shlex.quote(final_meta_json)} > "$TMP"
            mv -f "$TMP" "$DIR/LOCK/metadata"
            chmod -R g+rwsX "$DIR/LOCK" 2>/dev/null || true
        """).strip()
        update_proc = ssh_run(state, update_meta, check=False)
        if update_proc.returncode != 0:
            sys.stderr.write(update_proc.stderr.decode("utf-8", errors="replace"))
            die(f"metadata update failed (exit {update_proc.returncode})")
        rsync_down(state, remote_path(state, name, "current.blend"), local_blend)
    except BaseException:
        try:
            ssh_run(state, release, check=False)
        except BaseException:
            pass
        raise

    state.setdefault("checkouts", {})[name] = {
        "epoch_at_checkout": epoch,
        "description": args.message,
        "checked_out_at": final_meta["timestamp"],
        "lock_uuid": lock_uuid,
    }
    save_state(state)
    print(
        f"checked out {name!r} at epoch {epoch} → "
        f"{local_blend.relative_to(REPO_ROOT)}"
    )
    return 0


REMOTE_CHECKIN_PROBE_SCRIPT = r"""
set -u
ROOT=__ROOT__
NAME=__NAME__
DIR="$ROOT/$NAME"
if [ ! -d "$DIR" ]; then
  echo "NOMAP" >&2
  exit 3
fi
EPOCH="$(cat "$DIR/epoch" 2>/dev/null || echo '?')"
if [ -d "$DIR/LOCK" ] && [ -f "$DIR/LOCK/metadata" ]; then
  printf 'EPOCH %s\n' "$EPOCH"
  cat "$DIR/LOCK/metadata"
elif [ -d "$DIR/LOCK" ]; then
  printf 'EPOCH %s\n' "$EPOCH"
  echo '{}'
else
  printf 'EPOCH %s\nNOLOCK\n' "$EPOCH"
fi
"""


def cmd_checkin(args: argparse.Namespace, state: dict) -> int:
    name = validate_map_name(args.name)
    local_blend = LOCAL_MAPS_DIR / f"{name}.blend"
    if not local_blend.is_file():
        die(f"no local file at {local_blend}")

    local_record = state.get("checkouts", {}).get(name)
    if local_record is None and not args.force:
        die(
            f"no local checkout record for {name!r}. "
            "use 'new' for first-time uploads, or pass --force to override"
        )

    script = render_script(
        REMOTE_CHECKIN_PROBE_SCRIPT,
        root=shlex.quote(state["server_root"]),
        name=shlex.quote(name),
    )
    proc = ssh_run(state, script, check=False)
    if proc.returncode == 3:
        die(f"map {name!r} does not exist on server")
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die("probe failed")
    body = proc.stdout.decode("utf-8", errors="replace")

    epoch_line, _, rest = body.partition("\n")
    if not epoch_line.startswith("EPOCH "):
        die(f"unexpected probe response: {body!r}")
    try:
        remote_epoch = int(epoch_line.split(" ", 1)[1])
    except ValueError:
        die(f"server epoch unreadable: {epoch_line!r}")

    if rest.strip() == "NOLOCK":
        if not args.force:
            die(
                f"map {name!r} has no active lock on the server. "
                "pass --force to upload anyway (will create a new epoch)"
            )
        remote_meta: dict = {}
    else:
        try:
            remote_meta = json.loads(rest) if rest.strip() else {}
        except json.JSONDecodeError:
            remote_meta = {}

    holder = remote_meta.get("user")
    me = state["server_user"]
    if holder and holder != me and not args.force:
        die(
            f"lock on {name!r} is held by {holder!r}, not you ({me!r}).\n"
            f"  {fmt_lock(remote_meta)}\n"
            "pass --force to override (will steal the lock)"
        )

    # UUID catches stolen-released-reacquired cycles where holder name still matches.
    expected_uuid = local_record.get("lock_uuid") if local_record else None
    remote_uuid = remote_meta.get("lock_uuid")
    if expected_uuid and remote_uuid and remote_uuid != expected_uuid \
            and not args.force:
        die(
            f"lock identity mismatch on {name!r}: this isn't the lock you "
            f"acquired (expected uuid={expected_uuid}, "
            f"server has uuid={remote_uuid}). "
            "Someone released and re-acquired this lock. "
            "pass --force to overwrite anyway."
        )

    if local_record is not None:
        if local_record["epoch_at_checkout"] != remote_epoch:
            if not args.force:
                die(
                    f"epoch mismatch: locally checked out at "
                    f"{local_record['epoch_at_checkout']}, server is at "
                    f"{remote_epoch}. Someone else changed this map. "
                    "pass --force to overwrite their work."
                )
            print(
                f"warning: forcing checkin despite epoch mismatch "
                f"(local {local_record['epoch_at_checkout']} vs "
                f"remote {remote_epoch})",
                file=sys.stderr,
            )

    incoming_name = f".incoming.{os.getpid()}.blend"
    incoming = remote_path(state, name, incoming_name)
    rsync_up(state, local_blend, incoming)

    new_epoch = remote_epoch + 1
    archive_name = f"{remote_epoch:04d}.blend"
    expect_arg = expected_uuid or ""
    finalize = textwrap.dedent(rf"""
        set -eu
        DIR={shlex.quote(remote_path(state, name))}
        INCOMING={shlex.quote(incoming_name)}
        ARCHIVE={shlex.quote(archive_name)}
        NEW_EPOCH={shlex.quote(str(new_epoch))}
        EXPECTED_EPOCH={shlex.quote(str(remote_epoch))}
        EXPECT_UUID={shlex.quote(expect_arg)}
        FORCE={shlex.quote("1" if args.force else "0")}

        # If epoch advanced we'd archive over someone else's history slot.
        CURRENT_EPOCH="$(cat "$DIR/epoch" 2>/dev/null || echo '?')"
        if [ "$FORCE" != "1" ] && [ "$CURRENT_EPOCH" != "$EXPECTED_EPOCH" ]; then
          echo "EPOCH_DRIFT $CURRENT_EPOCH" >&2
          exit 8
        fi

        # Fail-closed: empty/unreadable UUID is mismatch, never pass-through.
        if [ "$FORCE" != "1" ] && [ -n "$EXPECT_UUID" ]; then
          if [ ! -d "$DIR/LOCK" ]; then
            echo "LOCK_GONE" >&2
            exit 9
          fi
          if [ ! -f "$DIR/LOCK/metadata" ]; then
            echo "NO_META" >&2
            exit 10
          fi
          HOLDER_UUID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("lock_uuid",""))' "$DIR/LOCK/metadata" 2>/dev/null || true)"
          if [ -z "$HOLDER_UUID" ] || [ "$HOLDER_UUID" != "$EXPECT_UUID" ]; then
            echo "STOLEN $HOLDER_UUID" >&2
            exit 7
          fi
        fi

        # Hard-link the outgoing version into history before overwriting
        # current.blend so no window leaves current.blend missing.
        if [ -f "$DIR/current.blend" ]; then
          ln -f "$DIR/current.blend" "$DIR/history/.$ARCHIVE.new" 2>/dev/null \
            || cp "$DIR/current.blend" "$DIR/history/.$ARCHIVE.new"
          mv -f "$DIR/history/.$ARCHIVE.new" "$DIR/history/$ARCHIVE"
        fi
        # Stage epoch first so the visible swap is a single rename.
        printf '%s\n' "$NEW_EPOCH" > "$DIR/.epoch.tmp"
        mv -f "$DIR/$INCOMING" "$DIR/current.blend"
        mv -f "$DIR/.epoch.tmp" "$DIR/epoch"
        rm -rf "$DIR/LOCK"
        chmod -R g+rwsX "$DIR" 2>/dev/null || true
        chmod 2770 "$DIR" "$DIR/history" 2>/dev/null || true
    """).strip()
    fproc = ssh_run(state, finalize, check=False)
    if fproc.returncode == 7:
        die(
            f"refusing to checkin: lock on {name!r} was stolen between "
            "probe and finalize. Incoming upload remains on server."
        )
    if fproc.returncode == 8:
        actual = fproc.stderr.decode("utf-8", errors="replace").strip()
        die(
            f"refusing to checkin: epoch advanced from {remote_epoch} during "
            f"upload ({actual}). Sync and retry, or pass --force to overwrite."
        )
    if fproc.returncode == 9:
        die(
            f"refusing to checkin: lock on {name!r} disappeared between probe "
            "and finalize (someone abandoned or broke it). Pass --force to upload anyway."
        )
    if fproc.returncode == 10:
        die(
            f"refusing to checkin: lock metadata on {name!r} is missing. "
            "Pass --force to upload anyway."
        )
    if fproc.returncode != 0:
        sys.stderr.write(fproc.stderr.decode("utf-8", errors="replace"))
        die(f"finalize failed (exit {fproc.returncode})")

    state.get("checkouts", {}).pop(name, None)
    save_state(state)
    print(f"checked in {name!r}: epoch {remote_epoch} → {new_epoch}")
    return 0


REMOTE_ABANDON_SCRIPT = r"""
set -u
ROOT=__ROOT__
NAME=__NAME__
EXPECT_USER=__USER__
EXPECT_UUID=__UUID__
ALLOW_BREAK=__BRK__
DIR="$ROOT/$NAME"
if [ ! -d "$DIR/LOCK" ]; then
  echo "NOLOCK"
  exit 0
fi
# Rename LOCK to a unique victim — past this point a new mkdir LOCK by
# another client succeeds, and we operate on the dir we already claimed.
VICTIM="$DIR/.LOCK.victim.$$"
if ! mv -T "$DIR/LOCK" "$VICTIM" 2>/dev/null; then
  # Lost the race: someone else removed it first.
  echo "NOLOCK"
  exit 0
fi
HOLDER=""
HOLDER_UUID=""
if [ -f "$VICTIM/metadata" ]; then
  HOLDER="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("user",""))' "$VICTIM/metadata" 2>/dev/null || true)"
  HOLDER_UUID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("lock_uuid",""))' "$VICTIM/metadata" 2>/dev/null || true)"
fi
if [ "$ALLOW_BREAK" = "1" ]; then
  rm -rf "$VICTIM"
  echo "RELEASED"
  exit 0
fi
# UUID is the strong check; fall back to username for older clients.
OWNED=0
if [ -n "$EXPECT_UUID" ] && [ "$HOLDER_UUID" = "$EXPECT_UUID" ]; then
  OWNED=1
elif [ -z "$HOLDER_UUID" ] && [ -n "$HOLDER" ] \
        && [ "$HOLDER" = "$EXPECT_USER" ]; then
  OWNED=1
fi
if [ "$OWNED" = "1" ]; then
  rm -rf "$VICTIM"
  echo "RELEASED"
  exit 0
fi
# Not ours — restore the lock so the rightful holder isn't disrupted.
if mv -T "$VICTIM" "$DIR/LOCK" 2>/dev/null; then
  echo "OTHER $HOLDER"
  exit 5
fi
rm -rf "$VICTIM"
echo "OTHER $HOLDER"
exit 5
"""


def cmd_abandon(args: argparse.Namespace, state: dict) -> int:
    name = validate_map_name(args.name)
    expected_uuid = (
        state.get("checkouts", {}).get(name, {}).get("lock_uuid", "")
    )
    script = render_script(
        REMOTE_ABANDON_SCRIPT,
        root=shlex.quote(state["server_root"]),
        name=shlex.quote(name),
        user=shlex.quote(state["server_user"]),
        uuid=shlex.quote(expected_uuid),
        brk=shlex.quote("1" if args.break_ else "0"),
    )
    proc = ssh_run(state, script, check=False)
    out = proc.stdout.decode("utf-8", errors="replace").strip()
    if proc.returncode == 5:
        holder = out.split(" ", 1)[1] if " " in out else "?"
        die(
            f"lock on {name!r} is held by {holder!r}; "
            "pass --break to forcibly remove it"
        )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die("abandon failed")
    state.get("checkouts", {}).pop(name, None)
    save_state(state)
    if out == "NOLOCK":
        print(f"no lock to release for {name!r} (cleared local record)")
    else:
        print(f"released lock on {name!r}")
    return 0


REMOTE_SYNC_PROBE_SCRIPT = r"""
set -u
ROOT=__ROOT__
NAME=__NAME__
DIR="$ROOT/$NAME"
if [ ! -d "$DIR" ]; then
  echo "NOMAP" >&2
  exit 3
fi
cat "$DIR/epoch" 2>/dev/null || echo "?"
"""


def cmd_sync(args: argparse.Namespace, state: dict) -> int:
    name = validate_map_name(args.name)
    LOCAL_MAPS_DIR.mkdir(exist_ok=True)
    local_blend = LOCAL_MAPS_DIR / f"{name}.blend"

    if name in state.get("checkouts", {}) and not args.force:
        die(
            f"you have a local checkout of {name!r} (epoch "
            f"{state['checkouts'][name]['epoch_at_checkout']}). "
            "syncing would overwrite your edits — pass --force if intended"
        )

    script = render_script(
        REMOTE_SYNC_PROBE_SCRIPT,
        root=shlex.quote(state["server_root"]),
        name=shlex.quote(name),
    )
    proc = ssh_run(state, script, check=False)
    if proc.returncode == 3:
        die(f"map {name!r} does not exist on server")
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        die(f"probe failed (exit {proc.returncode})")
    epoch = proc.stdout.decode("utf-8", errors="replace").strip() or "?"

    rsync_down(state, remote_path(state, name, "current.blend"), local_blend)
    print(
        f"synced {name!r} at epoch {epoch} → "
        f"{local_blend.relative_to(REPO_ROOT)} (read-only, no lock)"
    )
    return 0


def cmd_blend2glb(args: argparse.Namespace, state: dict) -> int:
    name = validate_map_name(args.name)
    helper = REPO_ROOT / "scripts" / "blend2glb.sh"
    if not helper.is_file():
        die(f"helper script not found: {helper}")
    proc = subprocess.run([str(helper), name], cwd=REPO_ROOT)
    return proc.returncode


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="mapsync",
        description="Collaborative .blend map workflow over the CSE125 server.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    sp = sub.add_parser("list", help="list maps on the server")
    sp.set_defaults(func=cmd_list)

    sp = sub.add_parser("status", help="show local checkout state")
    sp.set_defaults(func=cmd_status)

    sp = sub.add_parser("new", help="first-time push of maps/<name>.blend")
    sp.add_argument("name")
    sp.add_argument("-m", "--message", required=True, help="why this map exists")
    sp.set_defaults(func=cmd_new)

    sp = sub.add_parser("checkout", help="lock a map and download its .blend")
    sp.add_argument("name")
    sp.add_argument("-m", "--message", required=True, help="why you're checking out")
    sp.set_defaults(func=cmd_checkout)

    sp = sub.add_parser("checkin", help="upload, archive, bump epoch, release lock")
    sp.add_argument("name")
    sp.add_argument(
        "--force",
        action="store_true",
        help="proceed even if local record is missing or epoch/holder mismatch",
    )
    sp.set_defaults(func=cmd_checkin)

    sp = sub.add_parser(
        "sync", help="download current.blend without locking (read-only)"
    )
    sp.add_argument("name")
    sp.add_argument(
        "--force",
        action="store_true",
        help="overwrite a local file even if you have an active checkout for it",
    )
    sp.set_defaults(func=cmd_sync)

    sp = sub.add_parser("abandon", help="release a lock without uploading")
    sp.add_argument("name")
    sp.add_argument(
        "--break",
        dest="break_",
        action="store_true",
        help="forcibly remove a lock held by another user",
    )
    sp.set_defaults(func=cmd_abandon)

    sp = sub.add_parser(
        "blend2glb", help="export maps/<name>.blend to maps/assets/<name>.glb"
    )
    sp.add_argument("name")
    sp.set_defaults(func=cmd_blend2glb)

    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    state = ensure_config(load_state())
    return args.func(args, state)


if __name__ == "__main__":
    sys.exit(main())
