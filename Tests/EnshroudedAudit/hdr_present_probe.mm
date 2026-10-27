/* SPDX-License-Identifier: Apache-2.0 */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct Sample { double submitted=0,scheduled=0,gpuEnd=0,presented=0; };
struct Samples { std::mutex lock; std::vector<Sample> values; Samples(int n):values(n){} };
static double percentile(std::vector<double> v,double f) { if(v.empty())return 0;std::sort(v.begin(),v.end());return v[(size_t)(f*(v.size()-1))]; }
int main() { @autoreleasepool {
    setvbuf(stdout,NULL,_IONBF,0);[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> queue=[device newCommandQueue];
    NSScreen*screen=NSScreen.mainScreen;int hz=(int)screen.maximumFramesPerSecond;if(hz<=0)hz=60;
    printf("{\"device\":\"%s\",\"screen_max_hz\":%d,\"edr_max\":%.3f}\n",device.name.UTF8String,hz,screen.maximumExtendedDynamicRangeColorComponentValue);
    NSWindow*window=[[NSWindow alloc]initWithContentRect:NSMakeRect(24,40,420,240) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.title=@"HDR timing probe — closes automatically";window.level=NSFloatingWindowLevel;window.releasedWhenClosed=NO;window.contentView.wantsLayer=YES;[window orderFront:nil];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE,0),^{ @autoreleasepool {
      const int frames=240;double period=1.0/hz;
      bool hdrOnly=getenv("PROBE_HDR_ONLY")!=nullptr;
      for(int pass=0;pass<(hdrOnly?4:8);pass++){ @autoreleasepool {
        bool hdr=hdrOnly||(pass&1)!=0;int buffers=hdrOnly?((pass==0||pass==3)?3:2):((pass&2)?3:2);bool sync=hdrOnly?false:(pass&4)!=0;
        auto samples=std::make_shared<Samples>(frames);__block CAMetalLayer*layer;
        dispatch_sync(dispatch_get_main_queue(),^{layer=[CAMetalLayer layer];layer.device=device;layer.pixelFormat=hdr?MTLPixelFormatRGB10A2Unorm:MTLPixelFormatBGRA8Unorm;layer.drawableSize=CGSizeMake(420,240);layer.frame=window.contentView.bounds;layer.opaque=YES;layer.framebufferOnly=NO;layer.maximumDrawableCount=buffers;layer.displaySyncEnabled=sync;layer.wantsExtendedDynamicRangeContent=hdr;CGColorSpaceRef cs=CGColorSpaceCreateWithName(hdr?kCGColorSpaceITUR_2100_PQ:kCGColorSpaceSRGB);layer.colorspace=cs;CGColorSpaceRelease(cs);window.contentView.layer=layer;});
        std::this_thread::sleep_for(std::chrono::milliseconds(500));double start=CACurrentMediaTime();int errors=0;
        for(int i=0;i<frames;i++){ @autoreleasepool {
          double remaining=start+i*period-CACurrentMediaTime();if(remaining>0)std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
          id<CAMetalDrawable> drawable=[layer nextDrawable];if(!drawable){errors++;continue;}
          id<MTLCommandBuffer> cb=[queue commandBuffer];MTLRenderPassDescriptor*rp=[MTLRenderPassDescriptor renderPassDescriptor];rp.colorAttachments[0].texture=drawable.texture;rp.colorAttachments[0].loadAction=MTLLoadActionClear;rp.colorAttachments[0].storeAction=MTLStoreActionStore;rp.colorAttachments[0].clearColor=MTLClearColorMake(hdr?0.25:0.08,hdr?0.25:0.08,hdr?0.25:0.08,1);id<MTLRenderCommandEncoder> enc=[cb renderCommandEncoderWithDescriptor:rp];[enc endEncoding];
          {std::lock_guard<std::mutex>g(samples->lock);samples->values[i].submitted=CACurrentMediaTime();}
          [cb addScheduledHandler:^(id<MTLCommandBuffer> c){
            {std::lock_guard<std::mutex>g(samples->lock);samples->values[i].scheduled=CACurrentMediaTime();}
            [drawable addPresentedHandler:^(id<MTLDrawable>d){std::lock_guard<std::mutex>g(samples->lock);samples->values[i].presented=d.presentedTime;}];
            [drawable present];
          }];
          [cb addCompletedHandler:^(id<MTLCommandBuffer> c){std::lock_guard<std::mutex>g(samples->lock);samples->values[i].gpuEnd=c.GPUEndTime;}];[cb commit];
        }}
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::vector<double>delay,interval,scheduled;int missed=0,valid=0;double last=0;
        {std::lock_guard<std::mutex>g(samples->lock);for(int i=30;i<frames-3;i++){auto&s=samples->values[i];if(s.presented<=0||s.gpuEnd<=0)continue;valid++;delay.push_back((s.presented-s.gpuEnd)*1000);scheduled.push_back((s.scheduled-s.submitted)*1000);if(last){double delta=(s.presented-last)*1000;if(delta>0){interval.push_back(delta);if(delta>period*1500)missed++;}}last=s.presented;}}
        printf("{\"hdr\":%s,\"buffers\":%d,\"vsync\":%s,\"valid\":%d,\"acquire_errors\":%d,\"interval_p50_ms\":%.3f,\"interval_p95_ms\":%.3f,\"gpu_to_present_p50_ms\":%.3f,\"gpu_to_present_p95_ms\":%.3f,\"schedule_p95_ms\":%.3f,\"intervals_over_1_5_refreshes\":%d}\n",hdr?"true":"false",buffers,sync?"true":"false",valid,errors,percentile(interval,.5),percentile(interval,.95),percentile(delay,.5),percentile(delay,.95),percentile(scheduled,.95),missed);
      }}
      dispatch_async(dispatch_get_main_queue(),^{[window close];[NSApp stop:nil];NSEvent*e=[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];[NSApp postEvent:e atStart:NO];});
    }});[NSApp run];return 0;
}}
