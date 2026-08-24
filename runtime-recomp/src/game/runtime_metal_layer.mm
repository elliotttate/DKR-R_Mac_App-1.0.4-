#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" void* dkr_create_metal_layer(void* ns_window_ptr) {
    if (ns_window_ptr == nullptr) {
        return nullptr;
    }
    NSWindow* window = (__bridge NSWindow*)ns_window_ptr;
    NSView* contentView = [window contentView];
    [contentView setWantsLayer:YES];

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.frame = contentView.bounds;
    metalLayer.contentsScale = window.backingScaleFactor;
    metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;

    [contentView.layer addSublayer:metalLayer];
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
