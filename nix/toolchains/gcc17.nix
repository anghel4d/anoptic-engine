{
  lib,
  stdenv,
  pkgs,
  overrideCC,
  buildPackages,
  targetPackages,
  callPackage,
  isl_0_20,
  noSysDirs ? true,
  wrapCC,
  nixpkgsSource,
}:

let
  gccPackageDirectory = "${nixpkgsSource}/pkgs/development/compilers/gcc";
  upstreamExpression = builtins.readFile "${gccPackageDirectory}/default.nix";
  versionsImport = "gccVersions = import ./versions.nix;";

  # Reuse the pinned nixpkgs GCC package, including its native/cross bootstrap
  # policy, while replacing only the closed version table it imports. Relocate
  # its other relative imports to their original pinned nixpkgs directory.
  expressionWithSnapshot =
    builtins.replaceStrings [ versionsImport ] [ "gccVersions = import ${./gcc17-versions.nix};" ]
      upstreamExpression;
  relocatedExpression =
    builtins.replaceStrings [ "./" ] [ "${gccPackageDirectory}/" ]
      expressionWithSnapshot;
  gccSnapshotFunction = import (builtins.toFile "anoptic-gcc17-package.nix" relocatedExpression);

  buildIsHost = lib.systems.equals stdenv.buildPlatform stdenv.hostPlatform;
  buildIsTarget = lib.systems.equals stdenv.buildPlatform stdenv.targetPlatform;
  hostIsTarget = lib.systems.equals stdenv.hostPlatform stdenv.targetPlatform;
  compilerStdenv =
    if (!buildIsTarget || !hostIsTarget) && stdenv.cc.isGNU then
      overrideCC stdenv buildPackages.gcc17
    else
      stdenv;
  bootstrapBuildStdenv = overrideCC buildPackages.stdenv buildPackages.gcc16;
  seedVersion = stdenv.cc.cc.version or stdenv.cc.version;
  buildBootstrapVersion = bootstrapBuildStdenv.cc.cc.version or bootstrapBuildStdenv.cc.version;

  unwrapped = (callPackage gccSnapshotFunction {
    inherit noSysDirs;
    majorMinorVersion = "17";
    _systemInfo = {
      inherit buildIsHost hostIsTarget;
    };
    reproducibleBuild = true;
    profiledCompiler = false;
    libcCross = if !buildIsTarget then targetPackages.libc or pkgs.libc else null;
    threadsCross = if !buildIsTarget then targetPackages.threads or pkgs.threads else { };
    isl = if stdenv.hostPlatform.isDarwin then null else isl_0_20;

    # Native GCC 17 is bootstrapped by nixpkgs's cached GCC 16.2 lane. A cross
    # GCC 17 then uses that native GCC 17 driver, as required for matching target
    # runtimes, while its build-machine tools remain on the GCC 16.2 bootstrap.
    stdenv = compilerStdenv;
    buildPackages = buildPackages // {
      stdenv = bootstrapBuildStdenv;
    };
  }).overrideAttrs (previous: {
    patches = (previous.patches or [ ]) ++ [ ./gcc17-meta-info-layout.patch ];
  });
in
assert lib.assertMsg (lib.hasInfix versionsImport upstreamExpression)
  "The pinned nixpkgs GCC expression changed its version-table boundary";
assert lib.assertMsg (lib.hasPrefix "16.2." seedVersion)
  "GCC 17 must start from the pinned GCC 16.2 lane (got ${seedVersion})";
assert lib.assertMsg (lib.hasPrefix "16.2." buildBootstrapVersion)
  "GCC 17 build tools must use the pinned GCC 16.2 lane (got ${buildBootstrapVersion})";
lib.lowPrio (wrapCC unwrapped)
