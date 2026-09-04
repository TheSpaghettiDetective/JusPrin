#import <Cocoa/Cocoa.h>

// Process-local only: keep Orca's palette query and Cocoa controls consistent
// without writing preferences or changing the user's system appearance.
void set_harness_appearance(bool dark)
{
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSMutableDictionary* arguments = [[defaults volatileDomainForName:NSArgumentDomain] mutableCopy];
    if (!arguments) arguments = [[NSMutableDictionary alloc] init];
    arguments[@"AppleInterfaceStyle"] = dark ? @"Dark" : @"Light";
    [defaults setVolatileDomain:arguments forName:NSArgumentDomain];
    [arguments release];
    // Called before wx startup for the palette, then again once NSApp exists.
    if (NSApp) NSApp.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}
