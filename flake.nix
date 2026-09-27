{
  description = "BetterEnchantmentEffects development toolchain";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      windowsSdk = pkgs: pkgs.stdenvNoCC.mkDerivation {
        name = "xwin-splat-sdk-10.0.26100-crt-14.44.17.14";
        nativeBuildInputs = [ pkgs.xwin ];
        dontUnpack = true;
        dontFixup = true;
        SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
        installPhase = ''
          xwin --accept-license --manifest-version 17 \
            --sdk-version 10.0.26100 --crt-version 14.44.17.14 \
            --cache-dir "$TMPDIR/xwin" splat --copy --output "$out"
        '';
        outputHashMode = "recursive";
        outputHashAlgo = "sha256";
        outputHash = "sha256-UFQjsFVBwcF/9e9tVFoG0Z1JySxyTnFqoaRwr/tUWzA=";
        meta.license = nixpkgs.lib.licenses.unfree;
      };
    in
    {
      packages = forAllSystems (pkgs: { windows-sdk = windowsSdk pkgs; });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.llvmPackages.clang-unwrapped
            pkgs.llvmPackages.llvm
            pkgs.lld
            pkgs.ccache
            pkgs.cmake
            pkgs.ninja
            (pkgs.python3.withPackages (ps: [ ps.numpy ps.pillow ]))
            pkgs.check-jsonschema
            pkgs.rsync
          ];

          shellHook = ''
            export BEEF_DEV_SHELL=1
            export CLANG_TIDY=${pkgs.llvmPackages.clang-unwrapped}/bin/clang-tidy
            export CLANG_QUERY=${pkgs.llvmPackages.clang-unwrapped}/bin/clang-query
            export CLANG_FORMAT=${pkgs.llvmPackages.clang-unwrapped}/bin/clang-format
            export NATIVE_CXX=${pkgs.clang}/bin/clang++
            export ASAN_SYMBOLIZER_PATH=${pkgs.llvmPackages.llvm}/bin/llvm-symbolizer
            export CCACHE_SLOPPINESS=pch_defines,time_macros
            # Rewrite each worktree's absolute paths to its own repo-relative
            # form before hashing, so worktrees and clones of the same commit
            # share ccache hits instead of missing on the checkout path.
            beef_root="$PWD"
            if beef_git_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
              export CCACHE_BASEDIR="$beef_git_root"
              beef_root="$beef_git_root"
            fi
            : "''${XWIN_DIR:=$beef_root/build/windows-sdk}"
            export XWIN_DIR
            unset beef_git_root beef_root
            git rev-parse --git-dir >/dev/null 2>&1 && git config core.hooksPath .githooks
          '';
        };
      });
    };
}
