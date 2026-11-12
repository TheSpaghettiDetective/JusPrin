# Welcome clip

`welcome-clip.mp4` is the clip on the first-launch welcome screen: 620×388, silent, looping. Home's page bundles it, so this copy is the source the bundle is built from.

`welcome-clip.webm` is the same clip in VP9, which the welcome screen offers first: Linux's web view (WebKitGTK) plays H.264 only when the user has installed extra codecs. Make it from the .mp4 whenever the clip changes:

    ffmpeg -i welcome-clip.mp4 -c:v libvpx-vp9 -b:v 0 -crf 34 -row-mt 1 -an welcome-clip.webm

It is a placeholder: an animatic of the four beats ("Show it what happened." / "It reads your photo, your file and your slice." / "It changes the settings for you." / "And slices again. Tonight."). The agent's words and the setting values in it were typed for the animatic, not recorded from the app. The clip that ships is to be a recording of the real app.

The clip shows two photos licensed CC BY 2.0, which requires a visible credit. The welcome screen shows these credits under the clip ("Photo credits"); change both places together.

- "Dual extrusion 3D printed #3DBenchy v01" by #3DBenchy, https://www.flickr.com/photos/3dbenchy/19273965123/, CC BY 2.0 (https://creativecommons.org/licenses/by/2.0/). Cropped.
- "Dual-print multi-part color 3D print of #3DBenchy v13" by #3DBenchy, https://www.flickr.com/photos/3dbenchy/19239738473/, CC BY 2.0 (https://creativecommons.org/licenses/by/2.0/). Cropped and resized.

The model in the clip is 3DBenchy, licensed CC0 (https://www.3dbenchy.com/license/).
