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

## Important links 
- [Group main document](https://docs.google.com/document/d/119IUXJaZzLB1Wnq-WjzROSS_ZlbwY8B1QAaQj5ue13c/edit?tab=t.ugr095ybe2nc) 
- [Brainstorm whiteboard](https://excalidraw.com/#room=e75b50a0442682a79f02,f1qUrFmzmgYeGmBa12QS0A)
