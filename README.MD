# Group 5 repo for SP26 Spring CSE 125

## Building
You can either use the nix flake's devshell, or install the packages yourself. The flake does not build the project.

You should install cmake, ninja, and a recent version of python

Run `git submodule init` and `git submodule update` to clone all the submodules (external libraries).

You can use build.sh to build the project. You can also cross compile using build-windows.sh and build-linux-gcc.sh. The builds should support MacOS and Linux, we aren't sure about Windows.

## LSP support
Use build_lsp.sh to make a compile_commands.json for clangd.  This should be run after the normal build.

## Formatting

Run ./format.sh to format your code before comitting please.

## libraries
We currently use GLFW, GLAD, ENet, EnTT, and Dear Imgui as external libraries for various functionality.

## Documentation
Run `doxygen Doxyfile` to generate HTML docs in `html/`.

## Website (Jekyll) local preview

The course website lives in `docs/` (Jekyll).   
**Note**: to render images, you must put the image into the `docs/assets/`, preferably in the respective weeks folder. For example, if your file is `'/assets/week2/season_map.png'`, 
```html
<img src="{{ '/assets/week2/season_map.png' | relative_url }}" alt="text">
```

### One-time setup
```bash
cd docs
gem install bundler
bundle install
```

### Run the local server
```bash
cd docs
bundle exec jekyll serve --livereload
```

Now open:

If _config.yml has baseurl: "/group5": http://127.0.0.1:4000/group5/

If you want it at root instead: run bundle exec jekyll serve --baseurl "" and open http://127.0.0.1:4000/

### Upload to cse125
```bash
# If jekyll is onlty local, go into doc file: 
bundle exec jekyll build && scp -P222 -r _site/*  jic098@cse125.ucsd.edu:/var/www/html/cse125/2026/cse125g5

# Else: 
jekyll build && scp -P222 -r _site/*  jaroot@cse125.ucsd.edu:/var/www/html/cse125/2026/cse125g5
```

## Assets

Assets are not tracked in git. They are stored on the CSE 125 server and synced manually.

Use your CSE 125 account (`CSE125_USER`, e.g. `shc067`) and your course SSH private key. Set the key once per shell session:

```bash
export CSE125_USER=shc067
export CSE125_SSH_KEY="$HOME/.ssh/id_ed25519_cse125"   # your key path
export RSYNC_RSH="ssh -p 222 -i $CSE125_SSH_KEY -o IdentitiesOnly=yes"
```

Or put the key path in `.mapsync.local.json` (gitignored) so `scripts/mapsync.py` picks it up automatically:

```json
{
  "server_user": "shc067",
  "ssh_identity_file": "/Users/you/.ssh/id_ed25519_cse125"
}
```

`mapsync` also checks `CSE125_SSH_KEY` and `./id_cse125` in the repo root (same as `build-dist.sh`).

### Download assets from server

Models (goose, dog, gurf, rat), sounds, skyboxes, props, etc.:

```bash
rsync -avz --progress -e "$RSYNC_RSH" \
  "${CSE125_USER}@cse125.ucsd.edu:/var/www/html/cse125/2026/cse125g5/assets_for_game/" \
  assets/
```

### Upload assets to server

```bash
rsync -avz --progress -e "$RSYNC_RSH" \
  assets/ \
  "${CSE125_USER}@cse125.ucsd.edu:/var/www/html/cse125/2026/cse125g5/assets_for_game/"
```

The `-a` flag preserves file attributes and handles directories recursively. The `-z` flag compresses data during transfer. rsync only transfers files that have changed, so repeated syncs are fast.

After syncing assets, pull the latest map and export `landscape.glb` for the client (see **Maps** below):

```bash
scripts/mapsync.py sync landscape
scripts/mapsync.py blend2glb landscape
```

## Maps

`.blend` map sources are not tracked in git. They live on the CSE 125 server,
and `scripts/mapsync.py` manages a lock-based checkout / checkin workflow so
only one person edits a map at a time.

### First-time setup

Create the local working directory if you do not already have it:

```bash
mkdir -p maps
```

The first `mapsync.py` command will ask for your CSE 125 username and store it
in `.mapsync.local.json`.

To see what exists on the server:

```bash
scripts/mapsync.py list
```

`list` shows map names, epochs, and whether a map is currently locked.

### Read-only workflow

If you just want the latest server copy without locking the map:

```bash
scripts/mapsync.py sync default
```

This downloads `maps/default.blend` in read-only spirit, but the file is still
just a normal local `.blend`. Do not edit it and assume you can safely upload
later; if you intend to make changes, do a real checkout first.

### Edit an existing map

1. Check the map out and provide a short reason:

```bash
scripts/mapsync.py checkout default -m "redo arena lighting"
```

2. Edit the local file:

```text
maps/default.blend
```

3. Optionally export a local `.glb` for testing:

```bash
scripts/mapsync.py blend2glb default
```

This writes `maps/assets/default.glb`. The shell helper
`scripts/blend2glb.sh default` does the same thing directly.

4. Upload your edited `.blend` and release the lock:

```bash
scripts/mapsync.py checkin default
```

5. Check your local checkout state if needed:

```bash
scripts/mapsync.py status
```

`checkin` uploads the new `.blend`, archives the previous server version,
bumps the map epoch, and removes the lock.

### Create a new map

Prepare a local source file first:

```bash
mkdir -p maps
cp /path/to/source.blend maps/my_new_map.blend
```

Create the remote map entry:

```bash
scripts/mapsync.py new my_new_map -m "initial import"
```

`new` uploads the first version, but it does not leave the map checked out. If
you want to keep working immediately, check it out right after:

```bash
scripts/mapsync.py checkout my_new_map -m "continue editing after import"
```

### Recovery commands

- `scripts/mapsync.py abandon <name>` releases a lock you hold without uploading.
- `scripts/mapsync.py abandon <name> --break` forcibly removes someone else's lock. Use it only after confirming the other user is actually gone.
- `scripts/mapsync.py checkin <name> --force` overrides holder and epoch checks and can overwrite the server copy. Avoid it unless you mean to do that.

### Caveats

- `sync` and `checkout` both write directly to `maps/<name>.blend`. If that file already exists and you care about it, move it aside first.
- Map names must start with an alphanumeric character and may contain only letters, numbers, `.`, `_`, and `-`.
- `blend2glb` requires Blender on your `PATH`, or a `BLENDER` environment variable pointing to the Blender binary.
- If `new` fails after creating the remote directory, the server may still contain a partial map entry. Coordinate before retrying blindly.

## Profiling

To diagnose performance and stuttering issues, you can compile the client and server with Profiling enabled using:

```bash
./build-profiling.sh
```

## Important links 
- [Group main document](https://docs.google.com/document/d/119IUXJaZzLB1Wnq-WjzROSS_ZlbwY8B1QAaQj5ue13c/edit?tab=t.ugr095ybe2nc) 
- [Brainstorm whiteboard](https://excalidraw.com/#room=e75b50a0442682a79f02,f1qUrFmzmgYeGmBa12QS0A)
