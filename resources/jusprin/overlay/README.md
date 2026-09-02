# Resource overlay

Files here replace the OrcaSlicer resource of the same name at runtime.
`Slic3r::var()` checks `overlay/images/` before `resources/images/`, so an
upstream call such as `ScalableBitmap(this, "OrcaSlicer_about", 125)` renders
the JusPrin artwork without any edit to the calling code. Keep the OrcaSlicer
file names; they are the lookup keys. An SVG here shadows an upstream PNG of
the same stem because the bitmap loaders try SVG first.

Current overlays: splash, About lockup, horizontal lockup, monochrome mark
(message dialogs), setup-wizard watermark, and the window icon PNGs.
