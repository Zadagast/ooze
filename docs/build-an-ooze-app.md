# Build an Ooze app

OozeKit is installed as `libooze-kit` with headers under `oozekit/`.

## Package lookup

Use pkg-config:

```sh
pkg-config --cflags --libs oozekit
```

That returns the compiler flags for the public headers and links against
`-looze-kit`.

## Starter template

The repo includes `examples/hello-ooze/`, a minimal Ooze app using
`OozeApplication` and `OozeApplicationWindow`.

Build it in a fresh project with:

```sh
meson setup build
ninja -C build
```

The template uses the same header set as real Ooze apps, so you can copy the
directory into a new project and start from there.
