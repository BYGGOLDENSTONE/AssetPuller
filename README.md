# Asset Puller

An Unreal Engine 5.7 editor plugin that pulls assets **by name** from a separate
"asset library" project into your current project — copying the asset **and its
entire transitive dependency chain** (materials, textures, physics assets, map
external actors, ...) while preserving folder paths so nothing ever breaks.

The typical workflow it solves: you keep one big library project containing all
your purchased asset packs (Synty, Fab/Marketplace packs, etc.). Instead of
migrating whole packs into every game project, you type `SM_Bld_House_01` in your
game project and get exactly that house — plus the materials and textures it
needs — nothing more.

## Highlights

- **The source project is never opened.** The plugin reads `.uasset`/`.umap`
  package headers straight from the library's Content folder on disk, so the
  data is always up to date — no stale registry, no second editor instance.
- **Full dependency closure.** Hard imports, soft references (optional toggle),
  map `_BuiltData`, and One-File-Per-Actor external actor/object packages are
  all resolved and copied.
- **Reference-safe by construction.** Files are copied with their relative
  Content paths preserved, so package names stay identical and references
  resolve without redirectors. Both projects must use the same engine version.
- **Never overwrites.** Anything that already exists in the target is skipped
  and reported. Interrupted copies can't leave corrupt files behind (temp file
  + atomic rename). Maps that already exist in the target never get actors
  injected into them.
- **Preview before copy.** A confirmation dialog lists what will be copied
  (with total size), what will be skipped, and anything missing in the source.
- **Live search** over the whole library (100k+ assets), comma-separated
  multi-name input, multi-select import.
- **Headless mode** via a commandlet for batch scripts and CI.

## Requirements

- Unreal Engine **5.7** (source and target projects on the same version)
- Windows (uses standard engine APIs; other platforms untested)
- A C++ toolchain (Visual Studio 2022) to build the plugin once

## Installation

1. Copy the `AssetPuller` folder into your project's `Plugins` directory:
   `YourProject/Plugins/AssetPuller`
2. Open the project. If prompted to rebuild the plugin, accept (requires
   Visual Studio). For Blueprint-only projects, build once from any C++-enabled
   project on the same engine version, then reuse the folder with its
   `Binaries` directory included — no further compilation needed.
3. Check *Edit → Plugins → Asset Puller* is enabled (it is by default).

## Setup

Point the plugin at your library once, in either place:

- **Project Settings → Plugins → Asset Puller → Source Content Folder**, or
- the *Source Content Folder* field at the top of the Asset Puller window.

This is the **Content folder of the library project** on disk, e.g.
`X:/Projects/AssetLibrary/Content`. The setting is saved per project in
`Config/DefaultEditor.ini`.

## Usage

1. Click the **Asset Puller** toolbar button (or *Window → Asset Puller*).
2. Type an asset name — the list filters as you type. Separate several names
   with commas. Select one or more results (Ctrl/Shift click, or *Select All*).
3. Click **Import Selected**. Review the confirmation dialog:
   - green — new assets that will be copied (with total size)
   - grey — already in your project, skipped
   - red — referenced but missing in the source library
   - a warning appears when the selection includes maps (they can pull
     thousands of dependencies)
4. Confirm. Assets appear in the Content Browser immediately; a summary
   notification shows copied/skipped counts. Details are logged under the
   `LogAssetPuller` category.

Added new packs to the library? Press **Rescan** — the index is rebuilt from
disk in seconds.

## Command line

```
UnrealEditor-Cmd.exe <project.uproject> -run=AssetPuller -Names=SM_A,SM_B
    [-Source=<library Content folder>] [-NoSoft] [-DryRun] [-VerifyLoad]
```

- `-Source` overrides the folder from Project Settings
- `-NoSoft` ignores soft references
- `-DryRun` prints the resolved plan without copying anything
- `-VerifyLoad` fully loads every copied package afterwards to prove the
  references are intact (useful in automated tests)

Exit code is non-zero when an asset name isn't found, a copy fails, or
`-VerifyLoad` finds a broken package.

## How it works (short version)

1. The source Content folder is scanned for `.uasset`/`.umap` files to build a
   name index (filenames only — fast).
2. For each requested asset, the plugin opens the package file with the
   engine's `FPackageReader` and reads its import table (hard dependencies)
   and soft package reference list, then walks the graph transitively.
   For maps it also collects `__ExternalActors__`/`__ExternalObjects__`
   packages by folder convention (they are not listed in the map's imports).
3. Every package that exists in the source but not in the target is copied to
   the same relative path under the target's Content folder. Because Unreal
   references packages by their `/Game/...` path, identical relative paths
   mean identical package names — references simply keep working.
4. The target's asset registry is rescanned for the new files, so they show up
   in the Content Browser without restarting the editor.

## Safety rules

- Existing target files are never overwritten (checked under both `.uasset`
  and `.umap` extensions to avoid duplicate package names).
- External actor/object packages are only copied when their owning map is
  copied in the same operation.
- The source folder must not overlap the target project's own Content folder.
- Copies go through a temp file and are renamed into place, so a crash or
  cancellation never leaves a truncated asset.

## License

MIT — see [LICENSE](LICENSE).
