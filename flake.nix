{
  description = "criu: checkpoint and restore in userspace";

  # Nixpkgs / NixOS version to use.
  inputs.nixpkgs.url = "nixpkgs/nixos-21.11";

  outputs = { self, nixpkgs }:
    let

      # Generate a user-friendly version number.
      version = builtins.substring 0 8 self.lastModifiedDate;

      # System types to support.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" "riscv64-linux" ];

      # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

      # Nixpkgs instantiated for supported system types.
      nixpkgsFor = forAllSystems (system:
        import nixpkgs {
          inherit system;
          overlays = [ self.overlay ];
        });

    in {

      # A Nixpkgs overlay.
      overlay = final: prev: {

        criu = with final;
          stdenv.mkDerivation rec {
            name = "criu-${version}";

            src = ./.;

            enableParallelBuilding = true;

            nativeBuildInputs = with nixpkgs; [
              git
              pkg-config
              docbook_xsl
              which
              makeWrapper
              docbook_xml_dtd_45
              python3
              python3.pkgs.wrapPython
              perl
            ];
            buildInputs = with nixpkgs; [
              protobuf
              protobufc
              asciidoc
              xmlto
              libpaper
              libnl
              libcap
              libnet
              iptables
            ];
            propagatedBuildInputs = with python3.pkgs; [
              python
              python3.pkgs.protobuf
            ];
            postPatch = with nixpkgs; ''
              substituteInPlace ./Documentation/Makefile \
                --replace "2>/dev/null" "" \
                --replace "-m custom.xsl" "-m custom.xsl --skip-validation -x ${docbook_xsl}/xml/xsl/docbook/manpages/docbook.xsl"
              substituteInPlace ./Makefile --replace "head-name := \$(shell git tag -l v\$(CRIU_VERSION))" "head-name = ${version}.0"
              ln -sf ${protobuf}/include/google/protobuf/descriptor.proto ./images/google/protobuf/descriptor.proto
            '';

            makeFlags = with nixpkgs; [
              "PREFIX=$(out)"
              "ASCIIDOC=${asciidoc}/bin/asciidoc"
              "XMLTO=${xmlto}/bin/xmlto"
            ];

            outputs = [ "out" "dev" "man" ];

            preBuild = ''
              # No idea why but configure scripts break otherwise.
              export SHELL=""
            '';

            hardeningDisable = [ "stackprotector" "fortify" ];
            # dropping fortify here as well as package uses it by default:
            # command-line>:0:0: error: "_FORTIFY_SOURCE" redefined [-Werror]

            postFixup = with nixpkgs; ''
              wrapProgram $out/bin/criu \
                --prefix PATH : ${lib.makeBinPath [ iptables ]}
              wrapPythonPrograms
            '';

          };

      };

      # Provide some binary packages for selected system types.
      packages =
        forAllSystems (system: { inherit (nixpkgsFor.${system}) criu; });

      # The default package for 'nix build'. This makes sense if the
      # flake provides only one package or there is a clear "main"
      # package.
      defaultPackage = forAllSystems (system: self.packages.${system}.criu);

      # Tests run by 'nix flake check' and by Hydra.
      checks = forAllSystems (system:
        with nixpkgsFor.${system};

        {
          inherit (self.packages.${system}) criu;

          # Additional tests, if applicable.
          test = stdenv.mkDerivation {
            name = "criu-test-${version}";

            buildInputs = [ criu ];

            unpackPhase = "true";

            buildPhase = ''
              echo 'running some integration tests'
              [[ $(criu) = 'Hello Nixers!' ]]
            '';

            installPhase = "mkdir -p $out";
          };
        }

        // lib.optionalAttrs stdenv.isLinux {
          # A VM test of the NixOS module.
          vmTest = with import (nixpkgs + "/nixos/lib/testing-python.nix") {
            inherit system;
          };

            makeTest {
              nodes = {
                client = { ... }: { imports = [ self.nixosModules.hello ]; };
              };

              testScript = ''
                start_all()
                client.wait_for_unit("multi-user.target")
                client.succeed("hello")
              '';
            };
        });

    };
}
