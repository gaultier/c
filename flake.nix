{
  description = "clone-gitlab-api";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/21.05";
    utils.url = "github:numtide/flake-utils";
    utils.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = { self, nixpkgs, ... }@inputs: inputs.utils.lib.eachSystem [
    "x86_64-linux" "i686-linux" "aarch64-linux" "x86_64-darwin"
  ] (system: let pkgs = import nixpkgs {
                   inherit system;
                 };
             in {
               devShell = pkgs.mkShell rec {
                 name = "my-c-project";

                 packages = with pkgs; [
                   # Development Tools
                   clang
                   gnumake
                 ];
               };
             });
}
