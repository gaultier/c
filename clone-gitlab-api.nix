with import <nixpkgs> { };

stdenv.mkDerivation {
  name = "clone-gitlab-api";
  nativeBuildInputs = [ clang gnumake ];
  buildInputs = [ curl ];
  src = ./.;
  buildPhase = "make -C clone-gitlab-api";
  installPhase = "make -C clone-gitlab-api install PREFIX=$out";
  meta = with lib; {
    homepage = "https://github.com/gaultier/c";
    description = ''
      test
    '';
    platforms = with platforms; linux ++ darwin;
  };
}
