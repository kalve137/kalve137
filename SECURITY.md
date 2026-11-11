# Security

## Supported versions

Only the latest release gets fixes. Kalve 137 is before 1.0, so a fix ships as a new release rather than a patch to an older one.

## Reporting a vulnerability

Report privately through [Report a vulnerability](https://github.com/kalve137/kalve137/security/advisories/new) on the Security tab. Don't open a public issue.

Include the Kalve 137 version, steps to reproduce and a file that triggers the problem if there is one. Expect a first reply within a week. Fixes are credited in the release notes unless you ask otherwise.

## Scope

Kalve 137 runs offline and opens no network connections. Files it reads are the untrusted input: `.k137` designs, project folders and imported STEP files. A file that crashes the app, hangs it or makes it write anywhere other than the chosen folders, its settings and its autosave data is in scope.
