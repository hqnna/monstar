{
  description = "Monstar — Wayland terminal emulator";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      linuxSystems = builtins.filter
        (system: (lib.systems.elaborate system).isLinux)
        lib.systems.flakeExposed;
      forAllSystems = lib.genAttrs linuxSystems;
      buildInputs = pkgs: [
        pkgs.wayland
        pkgs.wayland-scanner
        pkgs.wayland-protocols
        pkgs.fontconfig
        pkgs.freetype
        pkgs.harfbuzz
        pkgs.libxkbcommon
      ];
      # monstar-qt, the Qt tabs-and-splits frontend in qt/.
      qtBuildInputs = pkgs: [
        pkgs.qt6.qtbase
        pkgs.qt6.qtdeclarative
        pkgs.qt6.qtwayland
        pkgs.wayland
      ];
      qtNativeBuildInputs = pkgs: [
        pkgs.cmake
        pkgs.ninja
        pkgs.pkg-config
        pkgs.wayland-scanner
      ];
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.stdenv.mkDerivation (finalAttrs: {
            pname = "monstar";
            version = "1.1.0";
            src = self;

            nativeBuildInputs = [ pkgs.zig pkgs.pkg-config pkgs.ncurses ];
            buildInputs = buildInputs pkgs;

            zigDeps = pkgs.zig.fetchDeps {
              inherit (finalAttrs) src pname version;
              fetchAll = true;
              hash = "sha256-O9qvv/Mag7ATJYNACN2DeB8q/HRhm52IHQVk6F9M20c=";
            };

            postConfigure = ''
              ln -s ${finalAttrs.zigDeps} "$ZIG_GLOBAL_CACHE_DIR/p"
            '';

            doInstallCheck = true;
            installCheckPhase = ''
              TERMINFO="$out/share/terminfo" infocmp -x monstar >/dev/null
            '';

            passthru.terminfo = pkgs.stdenv.mkDerivation {
              pname = "monstar-terminfo";
              inherit (finalAttrs) version;
              src = finalAttrs.finalPackage;

              dontUnpack = true;
              dontFixup = true;

              installPhase = ''
                runHook preInstall
                install -d "$out/share/terminfo"
                cp -r "$src/share/terminfo/." "$out/share/terminfo"
                runHook postInstall
              '';

              doInstallCheck = true;
              nativeBuildInputs = [ pkgs.ncurses ];
              installCheckPhase = ''
                TERMINFO="$out/share/terminfo" infocmp -x monstar >/dev/null
              '';
            };
          });
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = lib.getExe' self.packages.${system}.default "monstar";
        };
      });

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = [ pkgs.zig pkgs.pkg-config pkgs.ncurses ];
            buildInputs = buildInputs pkgs;
          };

          qt = pkgs.mkShell {
            nativeBuildInputs = [ pkgs.zig pkgs.pkg-config pkgs.ncurses ] ++ qtNativeBuildInputs pkgs;
            buildInputs = buildInputs pkgs ++ qtBuildInputs pkgs;
            # Let an unwrapped development build find Qt's QML modules and
            # platform plugins.
            shellHook = ''
              export QML_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/${pkgs.qt6.qtbase.qtQmlPrefix}:${pkgs.qt6.qtwayland}/${pkgs.qt6.qtbase.qtQmlPrefix}"
              export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtwayland}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtdeclarative}/${pkgs.qt6.qtbase.qtPluginPrefix}"
            '';
          };
        });
    };
}
