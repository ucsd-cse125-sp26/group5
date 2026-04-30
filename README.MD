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

### Download assets from server

```bash
rsync -avz --progress -e 'ssh -p 222' jaroot@cse125.ucsd.edu:/var/www/html/cse125/2026/cse125g5/assets_for_game/ assets/
```

### Upload assets to server

```bash
rsync -avz --progress -e 'ssh -p 222' assets/ jaroot@cse125.ucsd.edu:/var/www/html/cse125/2026/cse125g5/assets_for_game/
```

The `-a` flag preserves file attributes and handles directories recursively. The `-z` flag compresses data during transfer. rsync only transfers files that have changed, so repeated syncs are fast.

## Profiling

To diagnose performance and stuttering issues, you can compile the client and server with Tracy Profiling enabled using:

```bash
./build-profiling.sh
```

## Important links 
- [Group main document](https://docs.google.com/document/d/119IUXJaZzLB1Wnq-WjzROSS_ZlbwY8B1QAaQj5ue13c/edit?tab=t.ugr095ybe2nc) 
- [Brainstorm whiteboard](https://excalidraw.com/#room=e75b50a0442682a79f02,f1qUrFmzmgYeGmBa12QS0A)
