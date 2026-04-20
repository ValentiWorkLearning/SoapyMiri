#pragma once

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Logger.h>
#include <SoapySDR/Types.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "mirisdr.h"
#include <blockingconcurrentqueue.h>

#define DEFAULT_BUFFER_LENGTH (2304 * 8 * 2)
#define DEFAULT_NUM_BUFFERS 350
#define BYTES_PER_SAMPLE 2

typedef enum miriSampleFormat {
    MIRI_FORMAT_CS16,
    MIRI_FORMAT_CF32,
} miriSampleFormat;

class SoapyMiri : public SoapySDR::Device {
public:
    SoapyMiri(const SoapySDR::Kwargs &args);

    ~SoapyMiri(void);

    /*******************************************************************
     * Identification API
     ******************************************************************/

    std::string getDriverKey(void) const;

    std::string getHardwareKey(void) const;

    SoapySDR::Kwargs getHardwareInfo(void) const;

    /*******************************************************************
     * Channels API
     ******************************************************************/

    size_t getNumChannels(const int) const;

    bool getFullDuplex(const int direction, const size_t channel) const;

    /*******************************************************************
     * Stream API
     ******************************************************************/

    std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const;

    std::string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const;

    SoapySDR::ArgInfoList getStreamArgsInfo(const int direction, const size_t channel) const;

    SoapySDR::Stream *setupStream(const int direction, const std::string &format, const std::vector<size_t> &channels =
    std::vector<size_t>(), const SoapySDR::Kwargs &args = SoapySDR::Kwargs());

    void closeStream(SoapySDR::Stream *stream);

    size_t getStreamMTU(SoapySDR::Stream *stream) const;

    int activateStream(
            SoapySDR::Stream *stream,
            const int flags = 0,
            const long long timeNs = 0,
            const size_t numElems = 0);

    int deactivateStream(SoapySDR::Stream *stream, const int flags = 0, const long long timeNs = 0);

    int readStream(
            SoapySDR::Stream *stream,
            void *const *buffs,
            const size_t numElems,
            int &flags,
            long long &timeNs,
            const long timeoutUs = 100000);

    /*******************************************************************
     * Direct buffer access API
     ******************************************************************/

    size_t getNumDirectAccessBuffers(SoapySDR::Stream *stream);

    int getDirectAccessBufferAddrs(SoapySDR::Stream *stream, const size_t handle, void **buffs);

    int acquireReadBuffer(
            SoapySDR::Stream *stream,
            size_t &handle,
            const void **buffs,
            int &flags,
            long long &timeNs,
            const long timeoutUs = 100000);

    void releaseReadBuffer(
            SoapySDR::Stream *stream,
            const size_t handle);

    /*******************************************************************
     * Antenna API
     ******************************************************************/

    std::vector<std::string> listAntennas(const int direction, const size_t channel) const;

    void setAntenna(const int direction, const size_t channel, const std::string &name);

    std::string getAntenna(const int direction, const size_t channel) const;

    /*******************************************************************
     * Frontend corrections API
     ******************************************************************/

    bool hasDCOffsetMode(const int direction, const size_t channel) const;

    bool hasFrequencyCorrection(const int direction, const size_t channel) const;

    void setFrequencyCorrection(const int direction, const size_t channel, const double value);

    double getFrequencyCorrection(const int direction, const size_t channel) const;

    /*******************************************************************
     * Gain API
     ******************************************************************/

    std::vector<std::string> listGains(const int direction, const size_t channel) const;

    bool hasGainMode(const int direction, const size_t channel) const;

    void setGainMode(const int direction, const size_t channel, const bool automatic);

    bool getGainMode(const int direction, const size_t channel) const;

    void setGain(const int direction, const size_t channel, const double value);

    void setGain(const int direction, const size_t channel, const std::string &name, const double value);

    double getGain(const int direction, const size_t channel, const std::string &name) const;

    SoapySDR::Range getGainRange(const int direction, const size_t channel, const std::string &name) const;

    /*******************************************************************
     * Frequency API
     ******************************************************************/

    void setFrequency(
            const int direction,
            const size_t channel,
            const std::string &name,
            const double frequency,
            const SoapySDR::Kwargs &args = SoapySDR::Kwargs());

    double getFrequency(const int direction, const size_t channel, const std::string &name) const;

    std::vector<std::string> listFrequencies(const int direction, const size_t channel) const;

    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel, const std::string &name) const;

    SoapySDR::ArgInfoList getFrequencyArgsInfo(const int direction, const size_t channel) const;

    /*******************************************************************
     * Sample Rate API
     ******************************************************************/

    void setSampleRate(const int direction, const size_t channel, const double rate);

    double getSampleRate(const int direction, const size_t channel) const;

    std::vector<double> listSampleRates(const int direction, const size_t channel) const;

    SoapySDR::RangeList getSampleRateRange(const int direction, const size_t channel) const;

    void setBandwidth(const int direction, const size_t channel, const double bw);

    double getBandwidth(const int direction, const size_t channel) const;

    std::vector<double> listBandwidths(const int direction, const size_t channel) const;

    SoapySDR::RangeList getBandwidthRange(const int direction, const size_t channel) const;

    /*******************************************************************
     * Settings API
     ******************************************************************/

    SoapySDR::ArgInfoList getSettingInfo(void) const;

    void writeSetting(const std::string &key, const std::string &value);

    std::string readSetting(const std::string &key) const;

    /*******************************************************************
     * Utility
     ******************************************************************/

    static std::vector<SoapySDR::Kwargs> findMiriSDR(const SoapySDR::Kwargs &args);

private:

    mirisdr_dev_t *dev;

    //cached settings
    uint32_t deviceIdx;
    bool isOffsetTuning;
    double sampleRate;
    miriSampleFormat sampleFormat;
    std::map<std::string, mirisdr_hw_flavour_t> flavourMap = {
        { "Default", MIRISDR_HW_DEFAULT },
        { "SDRplay", MIRISDR_HW_SDRPLAY },
    };
    mirisdr_hw_flavour_t hwFlavour = MIRISDR_HW_DEFAULT;

public:
    void rx_callback(unsigned char *buf, uint32_t len);

private:

    struct RxBuffer
    {
        std::vector<int16_t> data;   // fixed capacity, fixed size after setup
        size_t validElems = 0;       // number of IQ elements currently valid
    };

    void rx_async_operation();

    // preallocated buffers
    std::vector<RxBuffer> rxBuffers_;

    // indices into rxBuffers_
    moodycamel::BlockingConcurrentQueue<size_t> freeQueue_;
    moodycamel::BlockingConcurrentQueue<size_t> filledQueue_;

    // async thread
    std::thread _rx_async_thread;

    // stream/config state
    int optBufferLength = DEFAULT_BUFFER_LENGTH;
    int optNumBuffers = DEFAULT_NUM_BUFFERS;

    // runtime state
    std::atomic<bool> streamActive_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> resetRequested_{false};
    std::atomic<bool> overflowEvent_{false};

    // current fragment state for readStream()
    size_t currentReadHandle_ = static_cast<size_t>(-1);
    const int16_t *currentReadPtr_ = nullptr;
    size_t remainingElems_ = 0;

    // optional: detect misuse if multiple readers call readStream simultaneously
    std::atomic_flag readInProgress_ = ATOMIC_FLAG_INIT;

    // helper
    void resetQueues();

};