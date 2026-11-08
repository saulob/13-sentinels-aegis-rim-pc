# Release security checks

Run these on the final artifacts, after the last package run. Repackaging changes the ZIP hash, so a scan only counts
for the exact file that gets published.

A clean ZIP scan does not cover the programs inside it: an engine can flag an executable in a ZIP that scanned clean.
Scan each file below on its own. Uploads stay manual: no script uploads, publishes or tags anything.

| File | Why |
| --- | --- |
| `13SentinelsAegisRimPC-v<version>-windows-x64.zip` | What Windows players download |
| `13_sentinels_launcher.exe`, from the staged folder of that ZIP | The launcher players open |
| `13_sentinels_aegis_rim.exe`, from the same staged folder | The game loader the launcher starts |

`tools\package.ps1` prints the ZIP, `13_sentinels_aegis_rim.exe` and `13_sentinels_launcher.exe` hashes when it
finishes. Check that each file you upload has the hash it printed. The staged folder is
`dist\13SentinelsAegisRimPC-v<version>-windows-x64`.

The port ships no self-updater and no Linux package, so there is no updater executable and no AppImage to scan. If
either is added, it joins this table and the hashes `package.ps1` prints.

The DLLs in the ZIP are third-party files copied unchanged; scan them separately only if an engine names one.

## Release report

Record all three values before publishing.

| File | SHA-256 | VirusTotal URL |
| --- | --- | --- |
| Windows ZIP | | |
| `13_sentinels_launcher.exe` | | |
| `13_sentinels_aegis_rim.exe` | | |

## Before publishing

Do not publish while an engine flags any of these files, unless the detection has been investigated to a conclusion
and that conclusion is written down.

Never work around a detection by packing, obfuscating or encrypting the program, by disabling a security product or
by asking players to add an exclusion.
