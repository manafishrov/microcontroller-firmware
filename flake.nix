{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    supportedSystems = [
      "x86_64-linux"
      "aarch64-linux"
      "aarch64-darwin"
    ];
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
  in {
    devShells = forAllSystems (system: let
      pkgs = nixpkgs.legacyPackages.${system};
      pico-sdk-with-submodules = pkgs.pico-sdk.override {
        withSubmodules = true;
      };
    in {
      default = pkgs.mkShell {
        buildInputs = with pkgs; [
          pkg-config
          cmake
          gcc-arm-embedded
          picotool
          pico-sdk-with-submodules
          clang
          clang-tools
          picocom
        ];
        PICO_SDK_PATH = "${pico-sdk-with-submodules}/lib/pico-sdk";
      };
    });
  };
}
