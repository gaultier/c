{
  description = "clone-gitlab-api";

  outputs = { self, nixpkgs }: {
    # curl.url = "github:curl/curl/610b96c6b315c76ba9ec1be189100170d3973fd7";

    packages.x86_64-macos.default = 
      with import nixpkgs { system = "x86_64-macos"; };
      stdenv.mkDerivation {
        name = "clone-gitlab-api";
        # baseInputs = [ clang gnumake ];
        #buildInputs = [ curl ];
        src = self;
        buildPhase = "make -C clone-gitlab-api";
        installPhase = "make -C clone-gitlab-api install PREFIX=$out";
        system = builtins.currentSystem;
      };

  };
}
