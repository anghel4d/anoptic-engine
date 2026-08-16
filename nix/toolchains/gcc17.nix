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

  unwrapped = callPackage gccSnapshotFunction {
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

    # Cross GCC builds compile fresh target runtimes. GCC requires the build
    # compiler to be the same major version as the cross compiler in that case.
    stdenv =
      if (!buildIsTarget || !hostIsTarget) && stdenv.cc.isGNU then
        overrideCC stdenv buildPackages.gcc17
      else
        stdenv;
  };
in
assert lib.assertMsg (lib.hasInfix versionsImport upstreamExpression)
  "The pinned nixpkgs GCC expression changed its version-table boundary";
lib.lowPrio (wrapCC unwrapped)
