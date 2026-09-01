#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>

#include <string.h>

#include "edge_sampler.h"

struct edge_sampler_request {
  uint32_t wid;
  uint64_t generation;
  struct adaptive_color_cache previous;
  uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT];
  edge_sampler_callback callback;
  void* context;
};

static struct edge_sampler_result edge_sampler_make_result(
    const struct edge_sampler_request* request,
    enum edge_sampler_status status) {
  struct edge_sampler_result result;
  memset(&result, 0, sizeof(result));
  result.status = status;
  result.wid = request->wid;
  result.generation = request->generation;
  result.analysis_status = ADAPTIVE_COLOR_INVALID_ARGUMENT;
  return result;
}

// Takes ownership of pixels. Its bytes remain valid until callback returns.
static void edge_sampler_deliver(struct edge_sampler_result result,
                                 CFDataRef pixels,
                                 edge_sampler_callback callback,
                                 void* context) {
  if (pixels) {
    CFIndex length = CFDataGetLength(pixels);
    if (length > 0) {
      result.frame.pixels = CFDataGetBytePtr(pixels);
      result.frame.pixels_size = (size_t)length;
    }
  }

  dispatch_async(dispatch_get_main_queue(), ^{
    callback(&result, context);
    if (pixels) CFRelease(pixels);
  });
}

static void edge_sampler_deliver_status(
    const struct edge_sampler_request* request,
    enum edge_sampler_status status) {
  edge_sampler_deliver(edge_sampler_make_result(request, status),
                       NULL,
                       request->callback,
                       request->context);
}

const char* edge_sampler_status_string(enum edge_sampler_status status) {
  switch (status) {
    case EDGE_SAMPLER_OK:
      return "ok";
    case EDGE_SAMPLER_UNSUPPORTED:
      return "unsupported";
    case EDGE_SAMPLER_CAPTURE_PERMISSION_DENIED:
      return "permission denied";
    case EDGE_SAMPLER_WINDOW_NOT_FOUND:
      return "window not found";
    case EDGE_SAMPLER_CONTENT_UNAVAILABLE:
      return "shareable content unavailable";
    case EDGE_SAMPLER_CAPTURE_FAILED:
      return "capture failed";
    case EDGE_SAMPLER_INVALID_IMAGE:
      return "invalid image";
    case EDGE_SAMPLER_ANALYSIS_FAILED:
      return "analysis failed";
  }
  return "unknown";
}

static void edge_sampler_process_image(
    const struct edge_sampler_request* request,
    CGImageRef image,
    NSError* error) {
  if (!image || error) {
    enum edge_sampler_status status = screen_capture_permission_allowed()
                                          ? EDGE_SAMPLER_CAPTURE_FAILED
                                          : EDGE_SAMPLER_CAPTURE_PERMISSION_DENIED;
    edge_sampler_deliver_status(request, status);
    return;
  }

  size_t width = CGImageGetWidth(image);
  size_t height = CGImageGetHeight(image);
  size_t bytes_per_row = CGImageGetBytesPerRow(image);
  if (!width || !height || !bytes_per_row
      || CGImageGetBitsPerComponent(image) != 8
      || CGImageGetBitsPerPixel(image) != 32) {
    edge_sampler_deliver_status(request, EDGE_SAMPLER_INVALID_IMAGE);
    return;
  }

  CGDataProviderRef provider = CGImageGetDataProvider(image);
  CFDataRef pixels = provider ? CGDataProviderCopyData(provider) : NULL;
  if (!pixels || CFDataGetLength(pixels) <= 0) {
    if (pixels) CFRelease(pixels);
    edge_sampler_deliver_status(request, EDGE_SAMPLER_INVALID_IMAGE);
    return;
  }

  struct edge_sampler_result result =
      edge_sampler_make_result(request, EDGE_SAMPLER_OK);
  result.frame.width = width;
  result.frame.height = height;
  result.frame.bytes_per_row = bytes_per_row;
  result.analysis_status = adaptive_color_analyze_bgra(
      CFDataGetBytePtr(pixels),
      (size_t)CFDataGetLength(pixels),
      width,
      height,
      bytes_per_row,
      &request->previous,
      request->fallback,
      &result.analysis);
  result.has_analysis = result.analysis_status == ADAPTIVE_COLOR_OK;
  if (!result.has_analysis) result.status = EDGE_SAMPLER_ANALYSIS_FAILED;

  edge_sampler_deliver(result, pixels, request->callback, request->context);
}

static void edge_sampler_capture_window(
    const struct edge_sampler_request* request,
    SCWindow* window) API_AVAILABLE(macos(14.0));

static void edge_sampler_capture_window(
    const struct edge_sampler_request* request,
    SCWindow* window) {
  SCContentFilter* filter =
      [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
  SCStreamConfiguration* configuration =
      [[SCStreamConfiguration alloc] init];
  if (!filter || !configuration) {
    [configuration release];
    [filter release];
    edge_sampler_deliver_status(request, EDGE_SAMPLER_CAPTURE_FAILED);
    return;
  }
  configuration.width = ADAPTIVE_COLOR_IMAGE_WIDTH;
  configuration.height = ADAPTIVE_COLOR_IMAGE_HEIGHT;
  configuration.pixelFormat = kCVPixelFormatType_32BGRA;
  configuration.scalesToFit = YES;
  configuration.preservesAspectRatio = NO;
  configuration.showsCursor = NO;
  configuration.capturesAudio = NO;
  configuration.ignoreShadowsSingleWindow = YES;
  configuration.ignoreGlobalClipSingleWindow = YES;
  configuration.colorSpaceName = kCGColorSpaceSRGB;
  configuration.captureResolution = SCCaptureResolutionNominal;
  if (@available(macOS 15.0, *)) {
    configuration.captureDynamicRange = SCCaptureDynamicRangeSDR;
  }

  struct edge_sampler_request request_copy = *request;
  [SCScreenshotManager
      captureImageWithFilter:filter
               configuration:configuration
           completionHandler:^(CGImageRef image, NSError* error) {
             @autoreleasepool {
               edge_sampler_process_image(&request_copy, image, error);
               [configuration release];
               [filter release];
             }
           }];
}

void edge_sampler_capture(uint32_t wid,
                          uint64_t generation,
                          const struct adaptive_color_cache* previous,
                          const uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT],
                          edge_sampler_callback callback,
                          void* context) {
  if (!callback) return;

  struct edge_sampler_request request;
  memset(&request, 0, sizeof(request));
  request.wid = wid;
  request.generation = generation;
  request.callback = callback;
  request.context = context;
  if (previous) request.previous = *previous;
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    request.fallback[side] = fallback ? fallback[side]
                                      : ADAPTIVE_COLOR_WHITE;
  }

  if (@available(macOS 14.0, *)) {
    if (!screen_capture_permission_allowed()) {
      edge_sampler_deliver_status(&request,
                                  EDGE_SAMPLER_CAPTURE_PERMISSION_DENIED);
      return;
    }

    [SCShareableContent
        getShareableContentExcludingDesktopWindows:YES
                               onScreenWindowsOnly:NO
                                  completionHandler:^(
                                      SCShareableContent* content,
                                      NSError* error) {
      @autoreleasepool {
        if (!content || error) {
          enum edge_sampler_status status =
              screen_capture_permission_allowed()
                  ? EDGE_SAMPLER_CONTENT_UNAVAILABLE
                  : EDGE_SAMPLER_CAPTURE_PERMISSION_DENIED;
          edge_sampler_deliver_status(&request, status);
          return;
        }

        SCWindow* target = nil;
        for (SCWindow* candidate in content.windows) {
          if (candidate.windowID == request.wid) {
            target = candidate;
            break;
          }
        }
        if (!target) {
          edge_sampler_deliver_status(&request,
                                      EDGE_SAMPLER_WINDOW_NOT_FOUND);
          return;
        }

        edge_sampler_capture_window(&request, target);
      }
    }];
    return;
  }

  edge_sampler_deliver_status(&request, EDGE_SAMPLER_UNSUPPORTED);
}
