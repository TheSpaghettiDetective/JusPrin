// The box the JusPrin server fills near the top of Preferences › Account
// (canvas "JusPrin v2", Credits & plans, decided 2026-11-06): the server's
// HTML, words and pictures only, in a frame.
//
// The frame must never run scripts. wxWidgets puts the app's bridge,
// window.wx, into every frame of the page, and the app cannot tell which
// frame sent a message, so a script in here could send anything Account
// can, such as signing out. allow-same-origin without allow-scripts runs
// none and still lets this page read the frame's height. The frame takes no
// clicks either, so a link in it can't open. The server's HTML brings its
// own styles, since the page's CSS doesn't reach in.
import { useEffect, useRef, useState } from 'react';

export function AccountBox({ html }: { html: string }) {
  const frame = useRef<HTMLIFrameElement>(null);
  const loaded = useRef(false);
  // Unknown until the content has loaded; the frame stays hidden till then.
  const [height, setHeight] = useState<number | null>(null);

  // The content can't report its height without scripts, so the page
  // measures it, again whenever the frame's width changes the wrapping.
  const measure = () => {
    const root = frame.current?.contentDocument?.documentElement;
    if (loaded.current && root)
      setHeight(Math.ceil(root.getBoundingClientRect().height));
  };
  useEffect(() => {
    const observer = new ResizeObserver(measure);
    observer.observe(frame.current!);
    return () => observer.disconnect();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // onLoad, not a listener added after mounting: the content may load first.
  return (
    <iframe ref={frame} className="prefs-box" title="From JusPrin" sandbox="allow-same-origin" srcDoc={html}
      onLoad={() => {
        loaded.current = true;
        measure();
      }}
      data-measuring={height === null ? '' : undefined} style={height === null ? undefined : { height }} />
  );
}
