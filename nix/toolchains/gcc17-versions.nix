let
  snapshotVersion = "17.0.0.20260809";
in
{
  fromMajorMinor =
    majorMinorVersion:
    {
      "17" = snapshotVersion;
    }
    ."${majorMinorVersion}";

  srcHashForVersion =
    version:
    {
      "${snapshotVersion}" = "sha256-x919orxEL+UERuDNH15vrS73oELPlUXkAgGUDZC7vUs=";
    }
    ."${version}";

  allMajorVersions = [ "17" ];
}
