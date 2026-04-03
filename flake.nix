{
  description = "CSE 125 Game Project";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { self, nixpkgs, ... }@inputs:
    inputs.flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          crossSystem = null;
        };
        mingw = pkgs.pkgsCross.mingwW64;
      in
      {
        devShells.default = pkgs.mkShell {
          name = "cse125-project";

          packages = with pkgs; [
            # Development Tools
            clang
            gcc
            cmake
            ninja
            gdb
            lldb
            clang-tools
            pkg-config

            # GLFW deps (Wayland)
            wayland
            wayland-protocols
            wayland-scanner
            libxkbcommon

            # GLFW deps (X11)
            libx11
            libxcursor
            libxi
            libxinerama
            libxrandr
            libxrender
            xorgproto

            # OpenGL
            libGL
            libGLU

            # Windows cross-compilation
            mingw.stdenv.cc
          ];

          shellHook = ''
            export CC=clang
            export CXX=clang++
            export CPLUS_INCLUDE_PATH="$(echo ${pkgs.gcc.cc}/include/c++/*/):$(echo ${pkgs.gcc.cc}/include/c++/*/x86_64-unknown-linux-gnu/):${pkgs.glibc.dev}/include:${pkgs.libGL.dev}/include''${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [
              pkgs.wayland
              pkgs.libxkbcommon
              pkgs.libGL
              pkgs.libx11
            ]}:$LD_LIBRARY_PATH"
          '';
        };
      }
    );
}
