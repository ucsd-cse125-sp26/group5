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
            gcc
            cmake
            ninja
            gdb
            lldb
            clang-tools

            # Windows cross-compilation
            mingw.stdenv.cc
          ];
        };
      }
    );
}
