{
  description = "Standalone mrustc-derived Rust compiler and Cargo driver";

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
          pname = "mrustc-toolchain";
          version = "0.1.0";

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
            MRUSTC_TARGET_VER=1.90 .build-nix/rustc/rustc -vV
            runHook postCheck
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 .build-nix/rustc/rustc "$out/bin/rustc"
            install -Dm755 .build-nix/cargo/cargo "$out/bin/cargo"
            runHook postInstall
          '';

          meta = {
            description = "Standalone mrustc-derived Rust compiler and Cargo-compatible driver";
            homepage = "https://github.com/pg83/mrustc";
            license = lib.licenses.mit;
            platforms = lib.platforms.linux;
          };
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
        in
        {
          default = (pkgs.mkShell.override { stdenv = pkgs.gcc16Stdenv; }) {
            inputsFrom = [ self.packages.${system}.toolchain ];
            hardeningDisable = [ "format" ];

            packages = with pkgs; [
              cacert
              curl
              git
              gnutar
              patch
              zstd
            ];

            SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
            GOFLAGS = "-mod=vendor";
            GOTOOLCHAIN = "local";

            shellHook = ''
              unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
              export CC=cc
              export CXX=c++
              export AR=ar
            '';
          };
        }
      );
    };
}
