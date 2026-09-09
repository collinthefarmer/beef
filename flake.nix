{
  description = "BetterEnchantmentEffects development toolchain";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.llvmPackages.clang-unwrapped
            pkgs.llvmPackages.llvm
            pkgs.lld
            pkgs.cmake
            pkgs.ninja
            (pkgs.python3.withPackages (ps: [ ps.numpy ps.pillow ]))
            pkgs.check-jsonschema
            pkgs.rsync
            pkgs.xwin
          ];

          shellHook = ''
            export BEEF_DEV_SHELL=1
            export CLANG_TIDY=${pkgs.llvmPackages.clang-unwrapped}/bin/clang-tidy
            export CLANG_QUERY=${pkgs.llvmPackages.clang-unwrapped}/bin/clang-query
            export NATIVE_CXX=${pkgs.clang}/bin/clang++
            export ASAN_SYMBOLIZER_PATH=${pkgs.llvmPackages.llvm}/bin/llvm-symbolizer
            : "''${XWIN_DIR:=$HOME/.xwin/splat}"
            export XWIN_DIR
          '';
        };
      });
    };
}
