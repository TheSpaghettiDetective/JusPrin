#include "SplashBar.hpp"

#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/window.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>

namespace Slic3r::GUI::JusPrin {

namespace {

CGColorRef cg_colour(const wxColour& colour)
{
    return CGColorCreateSRGB(colour.Red() / 255.0, colour.Green() / 255.0, colour.Blue() / 255.0, colour.Alpha() / 255.0);
}

} // namespace

void add_splash_bar(wxWindow& window, const wxRect& track, int segment_width, int sweep_ms,
                    const wxColour& track_colour, const wxColour& segment_colour)
{
    NSView* view = static_cast<NSView*>(window.GetHandle());
    if (view == nil)
        return;
    view.wantsLayer = YES;
    CALayer* root = view.layer;
    if (root == nil)
        return;

    // The backing layer follows the view's geometry, so its sublayers use the
    // view's coordinates: from the top-left when the view is flipped.
    const CGFloat height = view.bounds.size.height;
    const CGFloat top    = view.isFlipped ? track.y : height - track.y - track.height;
    const CGFloat radius = track.height / 2.0;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    CALayer* bar = [CALayer layer];
    bar.frame         = CGRectMake(track.x, top, track.width, track.height);
    bar.cornerRadius  = radius;
    bar.masksToBounds = YES;
    CGColorRef track_cg = cg_colour(track_colour);
    bar.backgroundColor = track_cg;
    CGColorRelease(track_cg);

    CALayer* segment = [CALayer layer];
    segment.anchorPoint  = CGPointMake(0, 0);
    segment.frame        = CGRectMake(0, 0, segment_width, track.height);
    segment.cornerRadius = radius;
    CGColorRef segment_cg = cg_colour(segment_colour);
    segment.backgroundColor = segment_cg;
    CGColorRelease(segment_cg);
    [bar addSublayer:segment];
    [root addSublayer:bar];

    CABasicAnimation* sweep = [CABasicAnimation animationWithKeyPath:@"position.x"];
    sweep.fromValue      = @(0);
    sweep.toValue        = @(track.width - segment_width);
    sweep.duration       = sweep_ms / 1000.0;
    sweep.autoreverses   = YES;
    sweep.repeatCount    = HUGE_VALF;
    sweep.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    sweep.removedOnCompletion = NO;
    [segment addAnimation:sweep forKey:@"sweep"];

    [CATransaction commit];
    // Startup blocks the main thread right after the splash is shown, before
    // the run loop would commit this; send it to the window server now.
    [CATransaction flush];
}

} // namespace Slic3r::GUI::JusPrin
