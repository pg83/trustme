{
  description = "trustme: a standalone Rust compiler and Cargo driver";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    {
      self,
      nixpkgs,
    }:
    let
      inherit (nixpkgs) lib;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = lib.genAttrs systems;

      nixpkgsFor = system: nixpkgs.legacyPackages.${system};

      mkToolchain =
        pkgs:
        pkgs.gcc16Stdenv.mkDerivation {
          name = "trustme-toolchain";

          src = self;

          dontConfigure = true;
          hardeningDisable = [ "format" ];

          nativeBuildInputs = with pkgs; [
            go
            pkg-config
            python3
          ];

          buildInputs = with pkgs; [
            zlib
            xxHash
          ];

          GOFLAGS = "-mod=vendor";
          GOTOOLCHAIN = "local";

          buildPhase = ''
            runHook preBuild
            export GOCACHE="$TMPDIR/go-cache"
            python3 ./build \
              -B .build-nix \
              -j "$NIX_BUILD_CORES" \
              rustc cargo
            runHook postBuild
          '';

          doCheck = true;

          checkPhase = ''
            runHook preCheck
            (
              cd cargo
              go test ./...
            )
            runHook postCheck
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 .build-nix/rustc/rustc "$out/bin/rustc"
            install -Dm755 .build-nix/cargo/cargo "$out/bin/cargo"
            runHook postInstall
          '';

          meta = {
            description = "trustme: a standalone Rust compiler and Cargo-compatible driver";
            homepage = "https://github.com/pg83/trustme";
            license = lib.licenses.mit;
            platforms = lib.platforms.linux;
          };
        };

      mkDevShell =
        pkgs: toolchain: stdenv: extraPackages: linkerFlags:
        (pkgs.mkShell.override { inherit stdenv; }) {
          inputsFrom = [ toolchain ];
          hardeningDisable = [ "format" ];

          packages =
            (with pkgs; [
              cacert
              curl
              git
              gnutar
              patch
              zstd
            ])
            ++ extraPackages;

          SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
          GOFLAGS = "-mod=vendor";
          GOTOOLCHAIN = "local";

          shellHook = ''
            unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
            export CC=cc
            export CXX=c++
            export AR=ar
            export LDFLAGS="${linkerFlags}"
          '';
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
          toolchain = mkToolchain pkgs;
        in
        {
          default = toolchain;
          inherit toolchain;
        }
      );

      checks = forAllSystems (system: {
        default = self.packages.${system}.toolchain;
      });

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
          toolchain = self.packages.${system}.toolchain;
        in
        {
          default = mkDevShell pkgs toolchain pkgs.gcc16Stdenv [ ] "";
          # Use the wrapped LLVM binutils so -fuse-ld=lld keeps Nix RPATHs.
          clang = mkDevShell pkgs toolchain pkgs.llvmPackages.libcxxStdenv [
            pkgs.llvmPackages.bintools
            # Clang otherwise only sees GCC's static libatomic archive.  A
            # generated executable linked against that archive and libc++'s
            # dynamic libatomic dependency defines the same IFUNC symbols and
            # cannot be loaded.  Put the shared runtime on the linker's search
            # path so the generated C++ translation units use one libatomic.
            pkgs.gcc.cc.lib
          ] "-fuse-ld=lld";
        }
      );
    };
}
