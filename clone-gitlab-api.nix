with import <nixpkgs> { };

stdenv.mkDerivation {
  name = "clone-gitlab-api";
  baseInputs = [ clang gnumake ];
  buildInputs = [ curl ];
  src = ./.;
  buildPhase = "make -C clone-gitlab-api";
  installPhase = "make -C clone-gitlab-api install PREFIX=$out";
  system = builtins.currentSystem;
}
