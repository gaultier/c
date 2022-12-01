{
  description = "clone-gitlab-api";

  inputs.nixpkgs.url = "nixpkgs/nixos-21.11";
  inputs.utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, utils }:
   utils.lib.eachDefaultSystem (system:
    let 
      pkgs = import nixpkgs { inherit system; };
      in 
      {

        # Provide some binary packages for selected system types.
        packages = forAllSystems (system:
          let
            pkgs = nixpkgsFor.${system};
            clone-gitlab-api = (with pkgs; stdenv.mkDerivation {
              name = "clone-gitlab-api";
              nativeBuildInputs = [ clang gnumake ];
              buildInputs = [ curl ];
              src = ./.;
              buildPhase = "make -C clone-gitlab-api";
              installPhase = "make -C clone-gitlab-api install PREFIX=$out";
            });
          in rec {
            defaultApp = utils.lib.mkApp {
              drv = defaultPackage;
            };
            defaultPackage = clone-gitlab-api;
            devShell = pkgs.mkShell {
              buildInputs = [
                clone-gitlab-api
              ];
            };
          }
        );
      };
}
