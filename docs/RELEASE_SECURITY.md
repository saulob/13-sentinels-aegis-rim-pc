# Release security checks

Run these on the final artifacts, after the last package run. Repackaging changes the ZIP hash, so a scan only counts
for the exact file that gets published.

A clean ZIP scan does not cover the programs inside it: an engine can flag an executable in a ZIP that scanned clean.
Scan each file below on its own. Uploads stay manual: no script uploads, publishes or tags anything.

| File | Why |
| --- | --- |
| `13SentinelsAegisRimPC-v<version>-windows-x64.zip` | What Windows players download |
| `13SentinelsAegisRimPC-v<version>-x86_64.AppImage` | What Linux players download |
| `13_sentinels_aegis_rim.exe`, from the staged folder of that ZIP | The one program players open: launcher and game |
| `trg-updater.exe`, from the same staged folder | The update helper that installs a newer release |

`tools\package-release.ps1` prepares both platforms and prints the SHA-256 of all four files when it finishes; it also
writes them into `dist\release-notes-v<version>.md`, whose VirusTotal entries stay PENDING until the scans are done.
`tools\package.ps1` alone prints the ZIP and executable hashes, `tools/package-linux.sh` the AppImage's. Check that each
file you upload has the hash printed. The staged folder is `dist\13SentinelsAegisRimPC-v<version>-windows-x64`. The
AppImage is one file: scan it as published.

The DLLs in the ZIP are third-party files copied unchanged; scan them separately only if an engine names one.

## Self-updater

Publish the ZIP with its `13SentinelsAegisRimPC-v<version>-windows-x64.zip.sha256` file and the AppImage with its
`13SentinelsAegisRimPC-v<version>-x86_64.AppImage.sha256` file, together, as assets of one release tagged `v<version>`
that is neither a draft nor a prerelease. Installed copies read
`https://api.github.com/repos/saulob/13-sentinels-aegis-rim-pc/releases/latest` and offer a version newer than their
own; a release without the ZIP under that exact name, or without a SHA-256 for it, is not offered or not installed.

What the updater checks before anything in the installation changes:

- Every request and redirect is HTTPS on the default port to `api.github.com`, `github.com`,
  `objects.githubusercontent.com` or `release-assets.githubusercontent.com`, without user information in the address. The
  package and checksum addresses must also start with this repository's `releases/download/` address.
- Transfers stop on a stall, after a total time limit, or when cancelled, and download to a temporary `.part` file in a
  temporary folder outside the installation. A failure removes that folder; a retry downloads again from the start.
- The ZIP must be exactly the published size and match the SHA-256 GitHub reports for the asset, or else the one in the
  published `.zip.sha256` file. A wrong or missing checksum stops the update.
- The ZIP is extracted to the temporary folder and refused if any entry is unsafe (absolute, `..`, links, reserved names)
  or protected: game data (`eboot.bin`, `sce_sys`, `.pkg`, `.cpk`, `.prx`, `CUSA19610` folders and others), user data
  (`user`, saves, trophies, `tsr.ini`, logs, caches) or archives. It must hold `13_sentinels_aegis_rim.exe`,
  `trg-updater.exe`, `SDL3.dll` and `libc++.dll`; `package.ps1` refuses to package a release without them.

The launcher then closes and `trg-updater.exe`, copied out of the installation, replaces only the files the package
holds, after backing up each one it replaces. Each new file is verified against its staged copy. If any replacement or
the restart of the launcher fails, the backed-up files are restored and the previous launcher is started again; the
helper's log is in the temporary update folder. Nothing outside the package's file list is changed or removed. The
launcher will not start the install while a game started from the same installation is still running.

The AppImage updates itself instead: the downloaded AppImage must have the published size and SHA-256 and be a type 2
AppImage. It is copied next to the installed AppImage file (`$APPIMAGE`, never its read-only mount, an extracted copy or
a folder under `/tmp/.mount_*`), given the installed file's mode plus execute permission, verified again, and renamed
over it after the old file is kept as a backup. The same file path is then started with `--launcher`; if it cannot
start, the backup is renamed back. `game`, `user` and `logs` next to the AppImage are never touched.

## Release report

Record all four values before publishing.

| File | SHA-256 | VirusTotal URL |
| --- | --- | --- |
| Windows ZIP | | |
| Linux AppImage | | |
| `13_sentinels_aegis_rim.exe` | | |
| `trg-updater.exe` | | |

## Before publishing

Do not publish while an engine flags any of these files, unless the detection has been investigated to a conclusion
and that conclusion is written down.

Never work around a detection by packing, obfuscating or encrypting the program, by disabling a security product or
by asking players to add an exclusion.
