import type { Appearance } from '../bridge/protocol';
import lockupLight from '@resources/jusprin/overlay/images/OrcaSlicer_horizontal_light.svg';
import lockupDark from '@resources/jusprin/overlay/images/OrcaSlicer_horizontal_dark.svg';

// The horizontal JusPrin lockup, the cone and the name, in the ink of the
// appearance; className sets where it sits and how tall it is.
export function Lockup({ appearance, className }: { appearance: Appearance; className: string }) {
  return <img className={className} src={appearance === 'dark' ? lockupDark : lockupLight} alt="JusPrin" />;
}
