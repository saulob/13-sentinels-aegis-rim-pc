# Bug reports

The launcher's **Bug Report** page sends one privacy-filtered game log, with a description, to a small report service
that only the developer reads. Nothing is sent until the player presses **Send report** and confirms. The service is a
Cloudflare Worker of its own, in [server/13-sentinels-bug-reports](../server/13-sentinels-bug-reports/README.md); it
shares nothing with any other project's service.

## What happens

1. The player describes the problem (10 to 500 characters) and picks one of the five newest game logs. The most recent
   gameplay session that has ended is chosen by default.
2. **Preview** shows exactly what would be sent. **Send report** builds the same report, shows it in the preview and asks
   to confirm. Only the confirmation opens a connection.
3. The launcher makes one outgoing HTTPS request, off the UI thread, with a short timeout. The service answers with a
   ticket (`13S-XXXXXX`) and a private receipt. The page shows the ticket; the receipt is kept in
   `user\bug_reports.json` and is sent only to ask about that one report.
4. **My Reports** lists the reports sent from this PC. Their status, the fixed version and the developer's reply are
   fetched only when the player presses **Refresh**; **What was sent** reads the stored report back the same way.

If the service cannot be reached, for example offline or blocked by a firewall, the page says so and keeps the text. **Save report** writes the same filtered report to `logs\reports\` for attaching to a GitHub issue. The game is
fully playable without any of this.

## What is sent

Exactly the fields of the preview, and nothing else:

| Field | Source |
|---|---|
| description | typed by the player, filtered, at most 500 characters |
| launcher_version | the port version, e.g. `0.1.0` |
| game_build, game_version, cusa, eboot_sha256 | the installed game (label, version, title id, hash of its executable) |
| operating_system | e.g. `Windows 11 (build 26200)` |
| renderer, gpu, driver, render_resolution | read from the start of the selected log, filtered |
| log_file | the name of the chosen log, `13_sentinels_aegis_rim_NNN.log` |
| log_clipped | whether only the end of the log was kept |
| log_text | the end of the chosen log after the privacy filter, at most 64 KiB |

Not sent: saves, game files, screenshots, settings files, crash dumps, other logs. No report is sent for a crash by
itself, and no log is sent without the player's confirmation.

## Privacy

`launcher/log_scrubber.cpp` filters the description and the log before they are shown or sent. It removes or masks
Windows and POSIX user names, computer names, profile and home folders, other local folders outside Windows' own, network
share paths, IPv4 and IPv6 addresses, email addresses, account numbers, tokens, passwords and other labelled secrets, and control
characters. It keeps what a report is for: versions, the operating system, the renderer, the graphics card, driver and
audio diagnostics, error messages, and the names (not the folders) of files. **Automatic filtering cannot promise to catch
every private value**, which is why the page shows the preview and says so.

- No telemetry, no usage statistics, no player counters, no heartbeat, no background traffic and no sessions are sent.
- There is no installation, player or account identity. Each report gets a random ticket and a random receipt; the
  service keeps only a salted hash of the receipt. Ownership is shown by presenting the receipt, nothing else.
- The service sees the connection's address while it answers, to limit abuse. It does not store it with the report: the
  per-address quota keeps a salted hash for one day, and the in-memory limits hold nothing after the object is evicted.
  The network is not anonymous: the address is visible to the connection's path and to Cloudflare.
- The newest 500 reports are kept for 180 days; their statuses for 365 days.

## Network behaviour

- Outgoing HTTPS only (port 443), TLS 1.2 or newer, no redirects: by WinHTTP with the system's certificate validation on
  Windows; on Linux by OpenSSL (bundled in the AppImage), which checks the certificate against the distribution's CA bundle
  and the host name. Both use the same time and size limits. Plain `http://` is refused.
- The launcher never listens, binds or opens a port, does not touch Windows Firewall, does not ask for administrator
  rights, and does not use UPnP, STUN or hole punching. An ordinary firewall allows outgoing HTTPS already; if a security
  program blocks it, the page says so and names port 443.
- Connections happen only from three buttons: Send report (after confirming), Refresh and What was sent. Nothing happens
  at startup, when the page opens, or in the background. `tests/bug_report_tests.cpp` checks this in the source.
- Timeouts: 8 s to connect, 15 s to send, 20 s to receive, 30 s in all.
- The game process has no network code: its PS4 network and HTTP calls answer "not connected".

## Service address

`kBugReportServiceUrl` in `launcher/game_profile.h` is `https://13-sentinels-bug-reports.saulobenigno.workers.dev`, so
ordinary users need no setting. `TSR_BUG_REPORT_URL` can override it for one run (https only). Deployment is in the
[service README](../server/13-sentinels-bug-reports/README.md). If the Worker is unreachable the page shows the failure,
keeps the text and offers **Save report**; Play and the game are unaffected.

## Tests

`tools\test.ps1 -Group Launcher` builds and runs `tsr-bug-report-tests` (no window, no network: a stand-in transport and
synthetic logs) and then the Worker's tests with Node's own runner on an in-memory database. The launcher test leaves the
request its client builds, and the Worker test checks that the service accepts it.

## Minimal production test

1. Launcher, Bug Report page: write a description, open the preview, press **Send report**, confirm.
2. The page shows "Report sent successfully" and a Report ID `13S-XXXXXX`.
3. Open `https://13-sentinels-bug-reports.saulobenigno.workers.dev/admin`, enter the token, open that ID: description,
   versions, OS, renderer, GPU and the sanitized log are there.
4. Set the status to **Investigating**, write a message to the player, press Save status.
5. In the launcher, My Reports, press **Refresh**: the status reads Investigating and "Message from developer" shows the text.
