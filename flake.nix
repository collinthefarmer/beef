{
  description = "BetterEnchantmentEffects development toolchain";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      searchDeps = pkgs: pkgs.writeShellApplication {
        name = "search-deps";
        runtimeInputs = [ pkgs.ripgrep pkgs.coreutils pkgs.gawk pkgs.git ];
        text = ''
          usage() {
            cat >&2 <<'EOF'
          usage: search-deps [--in SOURCE] [rg options] PATTERN
          Searches this plugin's pinned dependency sources and the workspace
          references with ripgrep, and nothing else. SOURCE is one of:
            deps        CommonLibSSE-NG, spdlog and rapidcsv (the default)
            commonlib   CommonLibSSE-NG only
            spdlog      spdlog only
            rapidcsv    rapidcsv only
            shaders     reference/community-shaders-src
            decompiled  decompiled/
            all         every source above
          The dependency sources are the copies CMake fetched at this plugin's
          pins, under build/Release/_deps (or build/Debug/_deps).
          EOF
          }

          source="deps"
          args=()
          while [ $# -gt 0 ]; do
            case "$1" in
              --in) [ $# -ge 2 ] || { usage; exit 2; }; source="$2"; shift 2 ;;
              --in=*) source="''${1#--in=}"; shift ;;
              -h|--help) usage; exit 0 ;;
              *) args+=("$1"); shift ;;
            esac
          done
          [ ''${#args[@]} -gt 0 ] || { usage; exit 2; }

          plugin=""
          dir="$PWD"
          while [ "$dir" != "/" ]; do
            if [ -f "$dir/flake.nix" ] && [ -f "$dir/CMakePresets.json" ]; then
              plugin="$dir"
              break
            fi
            dir="$(dirname "$dir")"
          done
          [ -n "$plugin" ] || { echo "search-deps: run it inside a plugin checkout" >&2; exit 2; }
          workspace="$(dirname "$(dirname "$plugin")")"

          deps_dir=""
          for config in Release Debug; do
            if [ -d "$plugin/build/$config/_deps" ]; then
              deps_dir="$plugin/build/$config/_deps"
              break
            fi
          done

          dependency() {
            if [ -z "$deps_dir" ] || [ ! -d "$deps_dir/$1-src" ]; then
              echo "search-deps: $1 is not fetched yet; run: cmake --preset windows-release" >&2
              exit 2
            fi
            roots+=("$deps_dir/$1-src")
          }
          reference() {
            if [ ! -d "$workspace/$1" ]; then
              echo "search-deps: $workspace/$1 does not exist" >&2
              exit 2
            fi
            roots+=("$workspace/$1")
          }

          roots=()
          case "$source" in
            deps) dependency commonlibsse; dependency spdlog; dependency rapidcsv ;;
            commonlib) dependency commonlibsse ;;
            spdlog) dependency spdlog ;;
            rapidcsv) dependency rapidcsv ;;
            shaders) reference reference/community-shaders-src ;;
            decompiled) reference decompiled ;;
            all)
              dependency commonlibsse; dependency spdlog; dependency rapidcsv
              reference reference/community-shaders-src; reference decompiled ;;
            *) echo "search-deps: unknown source '$source'" >&2; usage; exit 2 ;;
          esac

          for root in "''${roots[@]}"; do
            revision="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo "no git revision")"
            echo "# $root @ $revision"
          done

          max_lines=400
          status=0
          timeout 60 rg --max-columns 300 --max-columns-preview --max-count 50 \
            "''${args[@]}" "''${roots[@]}" |
            awk -v max="$max_lines" 'NR <= max { print } NR > max {
              print "search-deps: output cut at " max " lines; narrow the pattern or --in" > "/dev/stderr"
              exit
            }' || status=$?
          case "$status" in
            0) ;;
            1) echo "search-deps: no matches" >&2; exit 1 ;;
            124) echo "search-deps: stopped after 60 seconds; narrow the pattern or --in" >&2; exit 124 ;;
            *) exit "$status" ;;
          esac
        '';
      };
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
            (searchDeps pkgs)
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
