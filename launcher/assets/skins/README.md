# Character previews

Standard GTA San Andreas character previews downloaded from the
[open.mp skin reference](https://open.mp/docs/scripting/resources/skins):
`https://assets.open.mp/assets/images/skins/<model-id>.png`.

These previews identify existing game characters; they are not original launcher
artwork. GTA characters/assets belong to their respective owners. The launcher
does not include character model or texture files from GTA.

The shared catalog is `src/skins.json`: original GTA skins 0?299, excluding the
unused ID 74 (299 entries). Native metadata is generated with
`tools/generate_skin_catalog.py`. The original game's special characters need
streaming into temporary slots; these IDs are logical appearance IDs, not raw
slot numbers. SA-MP-only additional models 300?311 are not part of stock GTA.
