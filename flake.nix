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
        # Wrap clang-tidy so it can find system headers without polluting
        # CPLUS_INCLUDE_PATH globally (which would break other builds).
        clang-tidy-wrapped = pkgs.writeShellScriptBin "clang-tidy" ''
          export CPLUS_INCLUDE_PATH="${pkgs.gcc.cc}/include/c++/${pkgs.gcc.cc.version}:${pkgs.gcc.cc}/include/c++/${pkgs.gcc.cc.version}/${pkgs.stdenv.hostPlatform.config}:${pkgs.glibc.dev}/include:${pkgs.clang}/resource-root/include''${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
          exec ${pkgs.clang-tools}/bin/clang-tidy "$@"
        '';
      in
      {
        devShells.default = pkgs.mkShell {
          name = "cse125-project";

          packages = [
            clang-tidy-wrapped
          ] ++ (with pkgs; [
            # Development Tools
            clang
            gcc
            cmake
            ninja
            gdb
            lldb
            clang-tools
            pkg-config
            sccache

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
          ]);

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
