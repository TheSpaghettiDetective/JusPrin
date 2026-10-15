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

#import <WebKit/WebKit.h>

// What a web view shows, drawn to a PNG by WebKit itself. A screen capture
// needs the Screen Recording grant, which a harness started from a shell does
// not have; this needs none. Spins the run loop until WebKit answers.
bool snapshot_web_view(void* native_web_view, const char* path)
{
    WKWebView* view = (WKWebView*)native_web_view;
    __block NSImage* image = nil;
    __block bool     done  = false;
    [view takeSnapshotWithConfiguration:nil completionHandler:^(NSImage* snapshot, NSError*) {
        image = [snapshot retain];
        done  = true;
    }];
    NSDate* until = [NSDate dateWithTimeIntervalSinceNow:10];
    while (!done && [until timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    if (image == nil)
        return false;
    CGImageRef cg = [image CGImageForProposedRect:nullptr context:nil hints:nil];
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:cg];
    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    const bool written = [png writeToFile:[NSString stringWithUTF8String:path] atomically:YES];
    [bitmap release];
    [image release];
    return written;
}

// A window as its own views draw it, to a PNG: `native_view` is any view in
// it. Needs no Screen Recording grant.
bool snapshot_native_window(void* native_view, const char* path)
{
    NSView*           view  = ((NSView*)native_view).window.contentView;
    NSBitmapImageRep* image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    return [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:path]
                                                                                    atomically:YES];
}
