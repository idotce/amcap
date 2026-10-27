#include "camera.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#include <algorithm>
#include <stdexcept>

namespace camview {
std::vector<Device> enumerateDevices() {
    @autoreleasepool {
        std::vector<Device> result;
        // Keep the same enumeration API and order as OpenCV's AVFoundation backend.
        NSArray<AVCaptureDevice*>* devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
        int index = 0;
        for (AVCaptureDevice* device in devices) {
            result.push_back({device.localizedName.UTF8String, device.uniqueID.UTF8String, index++, {}});
        }
        return result;
    }
}
std::vector<Format> enumerateFormats(const Device& device, Backend) {
    @autoreleasepool {
        AVCaptureDevice* camera = [AVCaptureDevice deviceWithUniqueID:[NSString stringWithUTF8String:device.path.c_str()]];
        if (!camera) throw std::runtime_error("摄像头已移除");
        std::vector<Format> result;
        for (AVCaptureDeviceFormat* format in camera.formats) {
            const CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
            // AVFoundation uses big-endian FourCC; normalize for the shared label.
            const auto code = CMFormatDescriptionGetMediaSubType(format.formatDescription);
            const unsigned fourcc = ((code & 0xff) << 24) | ((code & 0xff00) << 8) |
                                    ((code & 0xff0000) >> 8) | ((code & 0xff000000) >> 24);
            for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
                // Endpoints are supported rates, not an invented list within the range.
                for (const CMTime time : {range.minFrameDuration, range.maxFrameDuration}) {
                    if (time.value <= 0 || time.timescale <= 0) continue;
                    Format entry{size.width, size.height, unsigned(time.timescale), unsigned(time.value), fourcc, -1};
                    auto found = std::find_if(result.begin(), result.end(), [&](const Format& old) {
                        return old.width == entry.width && old.height == entry.height && old.fourcc == entry.fourcc &&
                            old.fpsNumerator == entry.fpsNumerator && old.fpsDenominator == entry.fpsDenominator;
                    });
                    if (found == result.end()) result.push_back(entry);
                }
            }
        }
        return result;
    }
}
}
