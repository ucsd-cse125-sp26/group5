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

        mingwPthreadsStatic = mingw.windows.mingw_w64_pthreads.overrideAttrs (old: {
          dontDisableStatic = true;
          configureFlags = (old.configureFlags or [ ]) ++ [ "--enable-static" ];
        });
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

            # Python (for glad)
            (python3.withPackages (ps: [ ps.jinja2 ]))

            # Jekyll (docs site)
            ruby
            bundler

            # Jekyll (docs site)
            jekyll

            # Windows cross-compilation
            mingw.stdenv.cc
          ];

          shellHook = ''
            export MINGW_PTHREAD_STATIC_LIB_DIR="${mingwPthreadsStatic}/lib"
            export CC=clang
            export CXX=clang++
            export LD_LIBRARY_PATH="${
              pkgs.lib.makeLibraryPath [
                pkgs.wayland
                pkgs.libxkbcommon
                pkgs.libGL
                pkgs.libx11
              ]
            }:$LD_LIBRARY_PATH"
          '';
        };
      }
    );
}
