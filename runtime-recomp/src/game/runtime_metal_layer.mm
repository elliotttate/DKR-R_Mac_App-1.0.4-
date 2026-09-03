#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" void* dkr_create_metal_layer(void* ns_window_ptr) {
    if (ns_window_ptr == nullptr) {
        return nullptr;
    }
    NSWindow* window = (__bridge NSWindow*)ns_window_ptr;
    NSView* contentView = [window contentView];

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.frame = contentView.bounds;
    metalLayer.contentsScale = window.backingScaleFactor;
    metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;

    // Add as a sublayer: making this the view's hosted layer stops RT64's
    // output from being presented at all.
    [contentView setWantsLayer:YES];

    // The launcher's SDL_Renderer leaves its own layer (holding the last
    // launcher frame) in this view. Destroying the renderer does not remove it,
    // and AppKit re-compositing on focus loss brings that stale image back in
    // front of the live game. Drop every existing sublayer and clear the
    // parent's own contents so nothing is left to resurface.
    NSArray<CALayer*>* existing = [contentView.layer.sublayers copy];
    for (CALayer* sublayer in existing) {
        [sublayer removeFromSuperlayer];
    }
    contentView.layer.contents = nil;
    contentView.layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);

    [contentView.layer addSublayer:metalLayer];
    // Keep the game layer above anything AppKit or SDL adds back later.
    metalLayer.zPosition = 1000.0F;
    return (__bridge_retained void*)metalLayer;
}

extern "C" void dkr_destroy_metal_layer(void* layer_ptr) {
    if (layer_ptr == nullptr) {
        return;
    }
    CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)layer_ptr;
    [layer removeFromSuperlayer];
}
#endif
