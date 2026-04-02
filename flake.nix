{
  description = "A template for Nix based C++ project setup.";

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

          overlays = [ ];

          # config.allowUnfree = true;
        };
      in
      {
        devShells.default = pkgs.mkShell rec {
          name = "cse125-project";

          packages = with pkgs; [
            # Development Tools
            clang
            cmake
            ninja
            gdb
            lldb
            clang-tools
            clangStdenv
            clang
          ];
        };

        packages.default = pkgs.callPackage ./default.nix { };
      }
    );
}
