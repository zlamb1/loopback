let
    pkgs = import <nixpkgs> {};
    kernel = pkgs.linuxPackages_latest.kernel.dev;
    version = kernel.modDirVersion;
in
pkgs.mkShell {
    KDIR = "${kernel}/lib/modules/${version}/build";

    nativeBuildInputs = [
        pkgs.bear
        pkgs.pahole
    ];
}