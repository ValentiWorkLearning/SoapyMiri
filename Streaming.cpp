#include "SoapyMiri.hpp"
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Formats.hpp>
#include <cstring>
#include <chrono>
#include <stdexcept>
#include "ignore_unused_util.hpp"
#include <pthread.h>
#include <pthread/qos.h>


std::vector<std::string> SoapyMiri::getStreamFormats(const int direction, const size_t channel) const {
    ignore_unused(direction, channel);
    return {
        SOAPY_SDR_CS16,
        SOAPY_SDR_CF32,
    };
}

std::string SoapyMiri::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const {
    ignore_unused(direction, channel);
    fullScale = 1 << 15;
    return SOAPY_SDR_CS16;
}

SoapySDR::ArgInfoList SoapyMiri::getStreamArgsInfo(const int direction, const size_t channel) const {
    ignore_unused(direction, channel);
    SoapySDR::ArgInfoList streamArgs;

    SoapySDR::ArgInfo bufflenArg;
    bufflenArg.key = "bufflen";
    bufflenArg.value = std::to_string(DEFAULT_BUFFER_LENGTH);
    bufflenArg.name = "Buffer Size";
    bufflenArg.description = "Number of bytes per buffer, multiples of 512 only.";
    bufflenArg.units = "bytes";
    bufflenArg.type = SoapySDR::ArgInfo::INT;
    streamArgs.push_back(bufflenArg);

    SoapySDR::ArgInfo buffersArg;
    buffersArg.key = "buffers";
    buffersArg.value = std::to_string(DEFAULT_NUM_BUFFERS);
    buffersArg.name = "Ring buffers";
    buffersArg.description = "Number of preallocated buffers.";
    buffersArg.units = "buffers";
    buffersArg.type = SoapySDR::ArgInfo::INT;
    streamArgs.push_back(buffersArg);

    SoapySDR::ArgInfo asyncbuffsArg;
    asyncbuffsArg.key = "asyncBuffs";
    asyncbuffsArg.value = "0";
    asyncbuffsArg.name = "Async buffers";
    asyncbuffsArg.description = "Number of async usb buffers (advanced).";
    asyncbuffsArg.units = "buffers";
    asyncbuffsArg.type = SoapySDR::ArgInfo::INT;
    streamArgs.push_back(asyncbuffsArg);

    return streamArgs;
}

/*******************************************************************
 * Async thread work
 ******************************************************************/

static void _rx_callback(unsigned char *buf, uint32_t len, void *ctx) {
    auto *self = static_cast<SoapyMiri *>(ctx);
    self->rx_callback(buf, len);
}

void SoapyMiri::rx_async_operation() {
    pthread_set_qos_class_self_np(
        QOS_CLASS_USER_INTERACTIVE, 0);
    mirisdr_read_async(dev, &_rx_callback, this, optNumBuffers, optBufferLength);
}

void SoapyMiri::rx_callback(unsigned char *buf, uint32_t len) {
    if (!streamActive_.load(std::memory_order_acquire)) {
        return;
    }

    if (len > static_cast<uint32_t>(optBufferLength)) {
        overflowEvent_.store(true, std::memory_order_release);
        SoapySDR::log(SOAPY_SDR_WARNING,
                          "RX rx_callback:  len > optBufferLength");
        return;
    }

    size_t slot = 0;

    // Fast path: get a free buffer.
    if (!freeQueue_.try_dequeue(slot)) {
        // No free buffer -> consumer is behind.
        // Drop the oldest queued filled buffer and reuse it.
        size_t droppedSlot = 0;
        if (filledQueue_.try_dequeue(droppedSlot)) {
            rxBuffers_[droppedSlot].validElems = 0;
            freeQueue_.enqueue(droppedSlot);
        }

        if (!freeQueue_.try_dequeue(slot)) {
            // Extremely congested case: just drop this callback frame.
            overflowEvent_.store(true, std::memory_order_release);
            return;
        }

        overflowEvent_.store(true, std::memory_order_release);
    }

    auto &dst = rxBuffers_[slot];
    std::memcpy(dst.data.data(), buf, len);
    dst.validElems = static_cast<size_t>(len) / (sizeof(int16_t) * 2); // IQ pairs

    filledQueue_.enqueue(slot);
}

void SoapyMiri::resetQueues() {
    size_t slot = 0;

    while (filledQueue_.try_dequeue(slot)) {
        rxBuffers_[slot].validElems = 0;
        freeQueue_.enqueue(slot);
    }

    if (currentReadHandle_ != static_cast<size_t>(-1)) {
        rxBuffers_[currentReadHandle_].validElems = 0;
        freeQueue_.enqueue(currentReadHandle_);
        currentReadHandle_ = static_cast<size_t>(-1);
    }

    currentReadPtr_ = nullptr;
    remainingElems_ = 0;
}

/*******************************************************************
 * Stream API
 ******************************************************************/

SoapySDR::Stream *SoapyMiri::setupStream(
        const int direction,
        const std::string &format,
        const std::vector<size_t> &channels,
        const SoapySDR::Kwargs &args
) {
    if (!dev) {
        throw std::runtime_error("Trying to setupStream without an initialized MiriSDR!");
    }

    if (direction != SOAPY_SDR_RX) {
        throw std::runtime_error("LibMiriSDR supports only RX.");
    }

    if (channels.size() > 1 || (!channels.empty() && channels.at(0) != 0)) {
        throw std::runtime_error("setupStream invalid channel selection");
    }

    if (format == SOAPY_SDR_CS16) {
        sampleFormat = MIRI_FORMAT_CS16;
        SoapySDR_logf(SOAPY_SDR_WARNING, "SoapyMiri: CS16 format is untested!");
    } else if (format == SOAPY_SDR_CF32) {
        sampleFormat = MIRI_FORMAT_CF32;
    } else {
        throw std::runtime_error(
            "setupStream: invalid format '" + format + "', only CS16 and CF32 are supported by SoapyMiri."
        );
    }

    optBufferLength = DEFAULT_BUFFER_LENGTH;
    if (args.count("bufflen") != 0) {
        try {
            const int v = std::stoi(args.at("bufflen"));
            if (v > 0) {
                optBufferLength = v;
            }
        } catch (const std::exception &) {}
    }
    SoapySDR_logf(SOAPY_SDR_DEBUG, "SoapyMiri using buffer length %d", optBufferLength);

    optNumBuffers = DEFAULT_NUM_BUFFERS;
    if (args.count("buffers") != 0) {
        try {
            const int v = std::stoi(args.at("buffers"));
            if (v > 0) {
                optNumBuffers = v;
            }
        } catch (const std::exception &) {}
    }
    SoapySDR_logf(SOAPY_SDR_DEBUG, "SoapyMiri using %d buffers", optNumBuffers);

    streamActive_.store(false, std::memory_order_release);
    stopRequested_.store(false, std::memory_order_release);
    resetRequested_.store(false, std::memory_order_release);
    overflowEvent_.store(false, std::memory_order_release);

    currentReadHandle_ = static_cast<size_t>(-1);
    currentReadPtr_ = nullptr;
    remainingElems_ = 0;

    rxBuffers_.clear();
    rxBuffers_.resize(static_cast<size_t>(optNumBuffers));
    for (auto &b : rxBuffers_) {
        b.data.resize(static_cast<size_t>(optBufferLength) / sizeof(int16_t));
        b.validElems = 0;
    }

    freeQueue_ = moodycamel::BlockingConcurrentQueue<size_t>(static_cast<size_t>(optNumBuffers));
    filledQueue_ = moodycamel::BlockingConcurrentQueue<size_t>(static_cast<size_t>(optNumBuffers));

    for (size_t i = 0; i < rxBuffers_.size(); ++i) {
        freeQueue_.enqueue(i);
    }

    pthread_set_qos_class_self_np(
        QOS_CLASS_USER_INTERACTIVE, 0);

    return reinterpret_cast<SoapySDR::Stream *>(this);
}

void SoapyMiri::closeStream(SoapySDR::Stream *stream) {
    deactivateStream(stream, 0, 0);
    rxBuffers_.clear();
}

size_t SoapyMiri::getStreamMTU(SoapySDR::Stream *stream) const {
    ignore_unused(stream);
    return static_cast<size_t>(optBufferLength) / BYTES_PER_SAMPLE / 2;
}

int SoapyMiri::activateStream(
        SoapySDR::Stream *stream,
        const int flags,
        const long long timeNs,
        const size_t numElems
) {
    ignore_unused(stream, flags, timeNs, numElems);

    if (!dev) {
        return 0;
    }

    if (_rx_async_thread.joinable()) {
        return 0;
    }

    mirisdr_reset_buffer(dev);

    resetRequested_.store(true, std::memory_order_release);
    overflowEvent_.store(false, std::memory_order_release);
    stopRequested_.store(false, std::memory_order_release);
    streamActive_.store(true, std::memory_order_release);

    _rx_async_thread = std::thread(&SoapyMiri::rx_async_operation, this);
    return 0;
}

int SoapyMiri::deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs) {
    ignore_unused(stream, flags, timeNs);

    if (!dev) {
        return 0;
    }

    streamActive_.store(false, std::memory_order_release);
    stopRequested_.store(true, std::memory_order_release);

    if (_rx_async_thread.joinable()) {
        SoapySDR::log(SOAPY_SDR_WARNING,
                          "RX overflow: calling mirisdr_cancel_async");
        mirisdr_cancel_async(dev);
        _rx_async_thread.join();
    }

    resetQueues();
    return 0;
}

int SoapyMiri::readStream(
        SoapySDR::Stream *stream,
        void *const *buffs,
        const size_t numElems,
        int &flags,
        long long &timeNs,
        const long timeoutUs
) {
    ignore_unused(stream, timeNs, timeoutUs);

    if (readInProgress_.test_and_set(std::memory_order_acquire)) {
        SoapySDR::log(SOAPY_SDR_ERROR,
                      "Concurrent readStream() is not supported");
        return SOAPY_SDR_STREAM_ERROR;
    }

    auto clearGuard = [&]() {
        readInProgress_.clear(std::memory_order_release);
    };

    try {
        flags = 0;

        if (!streamActive_.load(std::memory_order_acquire)) {
            clearGuard();
            return SOAPY_SDR_STREAM_ERROR;
        }

        if (resetRequested_.exchange(false, std::memory_order_acq_rel)) {
            resetQueues();
        }

        // Non-fatal overflow: keep streaming, only warn once.
        if (overflowEvent_.exchange(false, std::memory_order_acq_rel)) {
            SoapySDR::log(SOAPY_SDR_WARNING,
                          "RX overflow: dropped stale samples, continuing");
        }

        // Need a new packet
        if (remainingElems_ == 0) {
            size_t handle = 0;

            // If queue empty: simply report no data now.
            if (!filledQueue_.try_dequeue(handle)) {
                clearGuard();
                return SOAPY_SDR_TIMEOUT;
            }

            currentReadHandle_ = handle;
            currentReadPtr_ = rxBuffers_[handle].data.data();
            remainingElems_ = rxBuffers_[handle].validElems;

            // Defensive: empty packet
            if (remainingElems_ == 0) {
                rxBuffers_[currentReadHandle_].validElems = 0;
                freeQueue_.enqueue(currentReadHandle_);
                currentReadHandle_ = static_cast<size_t>(-1);
                currentReadPtr_ = nullptr;

                clearGuard();
                return 0;
            }
        }

        const size_t returnedElems =
            std::min(remainingElems_, numElems);

        void *buff0 = buffs[0];

        if (sampleFormat == MIRI_FORMAT_CS16) {
            std::memcpy(buff0,
                        currentReadPtr_,
                        returnedElems * 2 * sizeof(int16_t));
        }
        else if (sampleFormat == MIRI_FORMAT_CF32) {
            float *ftarget = static_cast<float *>(buff0);

            for (size_t i = 0; i < returnedElems; ++i) {
                ftarget[i * 2 + 0] =
                    static_cast<float>(currentReadPtr_[i * 2 + 0]) *
                    (1.0f / 32768.0f);

                ftarget[i * 2 + 1] =
                    static_cast<float>(currentReadPtr_[i * 2 + 1]) *
                    (1.0f / 32768.0f);
            }
        }
        else {
            clearGuard();
            return SOAPY_SDR_NOT_SUPPORTED;
        }

        remainingElems_ -= returnedElems;
        currentReadPtr_ += returnedElems * 2;

        if (remainingElems_ != 0) {
            flags |= SOAPY_SDR_MORE_FRAGMENTS;
        }
        else if (currentReadHandle_ != static_cast<size_t>(-1)) {
            rxBuffers_[currentReadHandle_].validElems = 0;
            freeQueue_.enqueue(currentReadHandle_);

            currentReadHandle_ = static_cast<size_t>(-1);
            currentReadPtr_ = nullptr;
        }

        clearGuard();
        return static_cast<int>(returnedElems);
    }
    catch (...) {
        clearGuard();
        throw;
    }
}

/*******************************************************************
 * Direct buffer access API
 ******************************************************************/

size_t SoapyMiri::getNumDirectAccessBuffers(SoapySDR::Stream *stream) {
    ignore_unused(stream);
    return rxBuffers_.size();
}

int SoapyMiri::getDirectAccessBufferAddrs(SoapySDR::Stream *stream, const size_t handle, void **outBuffs) {
    ignore_unused(stream);

    if (handle >= rxBuffers_.size()) {
        return -1;
    }

    outBuffs[0] = static_cast<void *>(rxBuffers_[handle].data.data());
    return 0;
}

int SoapyMiri::acquireReadBuffer(
        SoapySDR::Stream *stream,
        size_t &handle,
        const void **outBuffs,
        int &flags,
        long long &timeNs,
        const long timeoutUs
) {
    ignore_unused(stream, timeNs);
    flags = 0;

    if (!streamActive_.load(std::memory_order_acquire)) {
        return SOAPY_SDR_STREAM_ERROR;
    }

    if (resetRequested_.exchange(false, std::memory_order_acq_rel)) {
        resetQueues();
    }

    if (overflowEvent_.exchange(false, std::memory_order_acq_rel)) {
        SoapySDR::log(SOAPY_SDR_WARNING,
                      "RX overflow: dropped stale samples, continuing");
    }

    const auto timeout = std::chrono::microseconds(timeoutUs > 0 ? timeoutUs : 0);
    if (!filledQueue_.wait_dequeue_timed(handle, timeout)) {
        return SOAPY_SDR_TIMEOUT;
    }

    outBuffs[0] = static_cast<const void *>(rxBuffers_[handle].data.data());
    return static_cast<int>(rxBuffers_[handle].validElems);
}

void SoapyMiri::releaseReadBuffer(SoapySDR::Stream *stream, const size_t handle) {
    ignore_unused(stream);

    if (handle >= rxBuffers_.size()) {
        return;
    }

    rxBuffers_[handle].validElems = 0;
    freeQueue_.enqueue(handle);
}