# Developing Ooze

Ooze has two tracks:

## Stable: the `.deb`

Build and install the desktop package:

```bash
./scripts/build-deb.sh
sudo apt install ./dist/ooze_*.deb
```

Log out, then pick **Ooze** at the login screen. This is the desktop you log
into.

## Dev: the devkit

Run Ooze in a window inside your current session:

```bash
./run-devkit.sh
```

For automatic rebuilds while editing:

```bash
./watch-devkit.sh
```

The devkit needs no reinstall and never touches your login session.

The old `/usr`-overwriting installer was removed because it could blank system
files, including the login session entry.
