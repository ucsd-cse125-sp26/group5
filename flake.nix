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

        ciPackages = [
          clang-tidy-wrapped
        ]
        ++ (with pkgs; [
          # Development Tools
          clang
          gcc
          cmake
          ninja
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

          # Audio 
          alsa-lib 
          libpulseaudio
          libjack2

          # Python (for glad)
          (python3.withPackages (ps: [ ps.jinja2 ]))

          # Windows cross-compilation
          mingw.stdenv.cc
        ]);

        devExtraPackages = with pkgs; [
          # Debuggers (local dev only)
          gdb
          lldb

          # Jekyll (docs site)
          ruby
          bundler
          jekyll
        ];

        commonShellHook = ''
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
      in
      {
        devShells.default = pkgs.mkShell {
          name = "cse125-project";
          packages = ciPackages ++ devExtraPackages;
          shellHook = commonShellHook;
        };

        devShells.ci = pkgs.mkShell {
          name = "cse125-project-ci";
          packages = ciPackages;
          shellHook = commonShellHook;
        };

        # Minimal server container: ships only the compiled `server` binary and
        # fetches the asset bundle from cse125 at startup.
        #
        # The binary is built ahead of time in the .#ci devShell and injected here
        # via $CSE125_SERVER_BIN, rather than built as a pure derivation: the C++
        # deps are git submodules and one (lib/soloud) ships a stale .gitmodules
        # that breaks Nix's `?submodules=1` source fetch. git (and the .#ci
        # devShell build) handle it fine. Build the image with:
        #   nix develop .#ci -c bash ./build-linux-gcc.sh -DCMAKE_BUILD_TYPE=Release
        #   CSE125_SERVER_BIN="$PWD/build-linux-gcc/server" nix build --impure .#serverImage
        # dockerTools scans the binary for store references, so its glibc/libstdc++
        # closure is pulled into the image automatically.
        packages =
          let
            # exeDir() canonicalizes /proc/self/exe, so the binary must be a real
            # file (not a store symlink) sitting where assets get extracted.
            serverApp =
              let bin = builtins.getEnv "CSE125_SERVER_BIN";
              in
              if bin == "" then
                throw "serverImage: set CSE125_SERVER_BIN to the prebuilt Linux server binary (e.g. \"$PWD/build-linux-gcc/server\") and build with `nix build --impure`"
              else
                pkgs.runCommand "cse125-server-app" { } ''
                  install -Dm755 ${/. + bin} $out/app/server
                '';

            entrypoint = pkgs.writeShellScriptBin "entrypoint" ''
              set -euo pipefail
              URL="''${ASSET_BUNDLE_URL:-https://cse125.ucsd.edu/2026/cse125g5/builds/asset-bundles/server-latest.zip}"
              MAP=/app/maps/assets/landscape.glb
              cd /app
              if [ -f "$MAP" ] && [ "''${FORCE_ASSET_REFRESH:-0}" != "1" ]; then
                echo "[entrypoint] map present at $MAP; skipping fetch"
              else
                echo "[entrypoint] fetching asset bundle: $URL"
                curl -fSL --retry 3 --retry-delay 2 -o /tmp/bundle.zip "$URL"
                echo "[entrypoint] extracting into /app"
                unzip -o /tmp/bundle.zip -d /app
                rm -f /tmp/bundle.zip
                if [ ! -f "$MAP" ]; then
                  echo "[entrypoint] FATAL: $MAP missing after extraction (bad bundle layout)" >&2
                  exit 1
                fi
              fi
              echo "[entrypoint] starting server (binds UDP 7777)"
              exec /app/server
            '';

            runtimeEnv = pkgs.buildEnv {
              name = "cse125-server-runtime";
              paths = [
                pkgs.bash
                pkgs.coreutils
                pkgs.curl
                pkgs.unzip
                entrypoint
              ];
            };

            serverImage = pkgs.dockerTools.buildImage {
              name = "git.rooty.dev/jrt/group5-server";
              tag = "latest";
              copyToRoot = [
                runtimeEnv
                pkgs.cacert
                serverApp
              ];
              extraCommands = "mkdir -p tmp";
              config = {
                Cmd = [ "/bin/entrypoint" ];
                WorkingDir = "/app";
                ExposedPorts = { "7777/udp" = { }; }; # ENet is UDP
                Env = [
                  "PATH=/bin"
                  "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
                ];
                Volumes = { "/app/maps" = { }; };
              };
            };
          in
          {
            inherit serverImage;
            default = serverImage;
          };
      }
    );
}
