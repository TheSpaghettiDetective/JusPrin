#include "SplashAnimation.hpp"

#include <wx/gdicmn.h>
#include <wx/imagwebp.h>
#include <wx/wfstream.h>
#include <wx/window.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

// A premultiplied sRGB copy of image, which keeps its alpha.
CGImageRef cg_image(const wxImage& image)
{
    const size_t           width = image.GetWidth(), height = image.GetHeight();
    const unsigned char*   rgb   = image.GetData();
    const unsigned char*   alpha = image.GetAlpha();
    CFMutableDataRef       data  = CFDataCreateMutable(nullptr, width * height * 4);
    CFDataSetLength(data, width * height * 4);
    UInt8* out = CFDataGetMutableBytePtr(data);
    for (size_t i = 0; i < width * height; ++i) {
        const unsigned a = alpha != nullptr ? alpha[i] : 255;
        out[4 * i]       = UInt8(rgb[3 * i] * a / 255);
        out[4 * i + 1]   = UInt8(rgb[3 * i + 1] * a / 255);
        out[4 * i + 2]   = UInt8(rgb[3 * i + 2] * a / 255);
        out[4 * i + 3]   = UInt8(a);
    }
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef   space    = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef        result   = CGImageCreate(width, height, 8, 32, width * 4, space, kCGImageAlphaPremultipliedLast,
                                               provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    CFRelease(data);
    return result;
}

} // namespace

void add_splash_animation(wxWindow& window, const wxRect& box, const std::string& webp_path)
{
    // wx's WebP decoder hands back every frame composited to the full canvas.
    std::vector<wxWebPAnimationFrame> frames;
    wxFFileInputStream                stream(wxString::FromUTF8(webp_path));
    if (!stream.IsOk() || !wxWEBPHandler().LoadAnimation(frames, stream, false) || frames.empty())
        throw std::runtime_error("the splash animation could not be read: " + webp_path);

    NSView* view = static_cast<NSView*>(window.GetHandle());
    if (view == nil)
        return;
    view.wantsLayer = YES;
    CALayer* root = view.layer;
    if (root == nil)
        return;

    // Frames that repeat are stored once with a longer duration, so each
    // frame's start time comes from the durations before it.
    double total_ms = 0;
    for (const wxWebPAnimationFrame& frame : frames)
        total_ms += frame.duration;
    NSMutableArray* images = [NSMutableArray arrayWithCapacity:frames.size()];
    NSMutableArray* times  = [NSMutableArray arrayWithCapacity:frames.size() + 1];
    double          at_ms  = 0;
    for (wxWebPAnimationFrame& frame : frames) {
        [times addObject:@(at_ms / total_ms)];
        at_ms += frame.duration;
        CGImageRef image = cg_image(frame.image);
        frame.image.Destroy();
        [images addObject:(id)image];
        CGImageRelease(image);
    }
    [times addObject:@(1.0)];

    const CGFloat top = view.isFlipped ? box.y : view.bounds.size.height - box.y - box.height;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    CALayer* layer = [CALayer layer];
    layer.frame    = CGRectMake(box.x, top, box.width, box.height);
    layer.contents = images[0];
    [root addSublayer:layer];

    CAKeyframeAnimation* play = [CAKeyframeAnimation animationWithKeyPath:@"contents"];
    play.values              = images;
    play.keyTimes            = times; // one more than the frames: the discrete mode's end
    play.calculationMode     = kCAAnimationDiscrete;
    play.duration            = total_ms / 1000.0;
    play.repeatCount         = HUGE_VALF;
    play.removedOnCompletion = NO;
    [layer addAnimation:play forKey:@"play"];

    [CATransaction commit];
    // Startup blocks the main thread right after the splash is shown, before
    // the run loop would commit this; send it to the window server now.
    [CATransaction flush];
}

} // namespace Slic3r::GUI::JusPrin
