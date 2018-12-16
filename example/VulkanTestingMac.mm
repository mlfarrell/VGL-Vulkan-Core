//
//  VulkanTestingMac.m
//  VulkanTesting
//

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import "VulkanTestingMac.h"

@interface DemoView : NSView
@end

@implementation DemoView

/** Indicates that the view wants to draw using the backing layer instead of using drawRect:.  */
-(BOOL) wantsUpdateLayer
{
  return YES;
}

/** Returns a Metal-compatible layer. */
+(Class) layerClass
{
  return [CAMetalLayer class];
}

/** If the wantsLayer property is set to YES, this method will be invoked to return a layer instance. */
-(CALayer *) makeBackingLayer
{
  CAMetalLayer *layer = [self.class.layerClass layer];
  CGSize viewScale = [self convertSizeToBacking: CGSizeMake(1.0, 1.0)];
  layer.contentsScale = MIN(viewScale.width, viewScale.height);
  //layer.contentsScale = 1.0;
  
  return layer;
}

@end

void addMetalLayer(void *nsWindow)
{
  NSWindow *win = (__bridge NSWindow *)nsWindow;
  
  win.contentView = [[DemoView alloc] initWithFrame:win.contentView.frame];
  win.contentView.wantsLayer = YES;
  chdir([NSBundle mainBundle].resourcePath.UTF8String);
}
