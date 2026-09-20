#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "menu/video_frame.hpp"
#include <filesystem>
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rush::video::detail {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) {
        return;
    }
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
    throw std::runtime_error(std::string(operation) + " failed (" + code + ")");
}

class MediaSession {
public:
    MediaSession() {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize COM");
        const auto result = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
        if (FAILED(result)) {
            CoUninitialize();
        }
        check(result, "Start Media Foundation");
    }
    ~MediaSession() {
        MFShutdown();
        CoUninitialize();
    }
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;
};

class LockedBuffer {
public:
    explicit LockedBuffer(IMFMediaBuffer* buffer) : buffer_(buffer) {
        check(buffer_->Lock(&bytes, nullptr, &length), "Lock decoded frame");
    }
    ~LockedBuffer() { buffer_->Unlock(); }
    LockedBuffer(const LockedBuffer&) = delete;
    LockedBuffer& operator=(const LockedBuffer&) = delete;
    BYTE* bytes = nullptr;
    DWORD length = 0;

private:
    ComPtr<IMFMediaBuffer> buffer_;
};

class VideoReader {
public:
    explicit VideoReader(const std::filesystem::path& path) {
        ComPtr<IMFAttributes> attributes;
        check(MFCreateAttributes(&attributes, 1), "Create reader attributes");
        check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
              "Enable video conversion");
        check(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader_),
              "Open preview file");
        check(reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE),
              "Disable unused streams");
        check(reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE),
              "Select video stream");
        ComPtr<IMFMediaType> type;
        check(MFCreateMediaType(&type), "Create video output type");
        check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video media type");
        check(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 output");
        check(
            reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get()),
            "Configure video decoder");
        read_format();
    }

    ComPtr<IMFSample> read(LONGLONG& timestamp, bool& end) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;
        check(reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags,
                                  &timestamp, &sample),
              "Decode video sample");
        if (flags & MF_SOURCE_READERF_ERROR) {
            throw std::runtime_error("Video reader reported a stream error");
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            const auto old_width = width_, old_height = height_;
            read_format();
            if (old_width != width_ || old_height != height_) {
                throw std::runtime_error("Preview dimensions changed from " +
                                         std::to_string(old_width) + "x" +
                                         std::to_string(old_height) + " to " +
                                         std::to_string(width_) + "x" + std::to_string(height_));
            }
        }
        end = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
        return sample;
    }

    void convert(IMFSample* sample, Frame& frame) {
        ComPtr<IMFMediaBuffer> buffer;
        check(sample->ConvertToContiguousBuffer(&buffer), "Read decoded pixels");
        const LockedBuffer locked(buffer.Get());
        const size_t pitch = static_cast<size_t>(stride_ < 0 ? -int64_t(stride_) : stride_);
        const size_t row = stride_ < 0 ? storage_height_ - top_ - height_ : top_;
        const size_t offset = row * pitch + size_t(left_) * 4;
        const uint64_t required = uint64_t(pitch) * (height_ - 1) + uint64_t(width_) * 4;
        if (uint64_t(left_ + width_) * 4 > pitch || offset > locked.length ||
            required > locked.length - offset) {
            throw std::runtime_error("Display aperture is outside decoded buffer");
        }
        convert_rgb32(locked.bytes + offset, locked.length - offset, width_, height_, stride_,
                      frame);
    }

    void rewind() {
        PROPVARIANT position{};
        position.vt = VT_I8;
        position.hVal.QuadPart = 0;
        check(reader_->SetCurrentPosition(GUID_NULL, position), "Rewind preview loop");
    }
    LONGLONG frame_duration() const { return duration_; }

private:
    void read_format() {
        ComPtr<IMFMediaType> type;
        check(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type),
              "Read video format");
        GUID subtype{};
        check(type->GetGUID(MF_MT_SUBTYPE, &subtype), "Read pixel format");
        if (subtype != MFVideoFormat_RGB32) {
            throw std::runtime_error("Decoder output is not RGB32");
        }
        check(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &storage_width_, &storage_height_),
              "Read video dimensions");
        if (!storage_width_ || !storage_height_ || storage_width_ > 8192 ||
            storage_height_ > 8192) {
            throw std::runtime_error("Unsupported preview dimensions (maximum 8192 x 8192)");
        }
        width_ = storage_width_;
        height_ = storage_height_;
        left_ = top_ = 0;
        // H.264 may expose coded padding (e.g. 540 visible rows in a 544-row
        // buffer). Only the display aperture contains valid image pixels.
        MFVideoArea aperture{};
        UINT32 aperture_size = 0;
        HRESULT aperture_result =
            type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&aperture),
                          sizeof(aperture), &aperture_size);
        if (FAILED(aperture_result)) {
            aperture_result =
                type->GetBlob(MF_MT_GEOMETRIC_APERTURE, reinterpret_cast<UINT8*>(&aperture),
                              sizeof(aperture), &aperture_size);
        }
        if (SUCCEEDED(aperture_result)) {
            if (aperture_size != sizeof(aperture) || aperture.OffsetX.value < 0 ||
                aperture.OffsetY.value < 0 || aperture.OffsetX.fract || aperture.OffsetY.fract ||
                aperture.Area.cx <= 0 || aperture.Area.cy <= 0 ||
                uint64_t(aperture.OffsetX.value) + aperture.Area.cx > storage_width_ ||
                uint64_t(aperture.OffsetY.value) + aperture.Area.cy > storage_height_) {
                throw std::runtime_error("Invalid or fractional video display aperture");
            }
            left_ = aperture.OffsetX.value;
            top_ = aperture.OffsetY.value;
            width_ = aperture.Area.cx;
            height_ = aperture.Area.cy;
        }
        UINT32 stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
            stride_ = static_cast<LONG>(stride);
        } else {
            check(
                MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, storage_width_, &stride_),
                "Read video row stride");
        }
        UINT32 numerator = 0, denominator = 0;
        if (SUCCEEDED(
                MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) &&
            numerator && denominator) {
            duration_ = (10'000'000LL * denominator) / numerator;
        }
    }
    ComPtr<IMFSourceReader> reader_;
    UINT32 width_ = 0, height_ = 0;
    UINT32 storage_width_ = 0, storage_height_ = 0, left_ = 0, top_ = 0;
    LONG stride_ = 0;
    LONGLONG duration_ = 333333;
};

}  // namespace rush::video::detail
