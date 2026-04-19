# SoapyMiri
## Installation

For Arch Linux, there's a package in the AUR: https://aur.archlinux.org/packages/soapymiri-git

For other distributions, clone and compile manually:

    cd SoapyMiri
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel


## Usage with SDR++

1. Check whether the SDR driver is visible after the build
```shell
export SOAPY_SDR_PLUGIN_PATH=${PWD}/build/
SoapySDRUtil --info
```
There should be soapyMiri-dev factory available in the utility log:
```
Available factories... rtlsdr, soapyMiri-dev
```

2. Rebuild SDR++ with Soapy driver support or take the prebuilt.
Unfortunately, for MacOS by default it's disabled.

Prebuilt version of it with the Soapy driver is available as well:
https://github.com/ValentiWorkLearning/SDRPlusPlus

So, when you've managed to build SDR++ with Soapy driver support, then simply run:
```export SOAPY_SDR_PLUGIN_PATH=...location of the build directory for SoapyMiri plugin```
and launch SDR++ from the shell.

Another option is to add symlink to the built plugin into the soapy plugins. If Soapy was installed via brew, the following location contains all of the plugins:
```log
/opt/homebrew/lib/SoapySDR/modules0.8/
```
So simply add a symlink:
```sh
ln -s ${PWD}/build/soapyMiriSupport-dev.so /opt/homebrew/lib/SoapySDR/modules0.8/soapyMiriSupport-dev.so
```

Verify that the soapy can find the newely added driver:
```shell
SoapySDRUtil --info
######################################################
##     Soapy SDR -- the SDR abstraction library     ##
######################################################

Lib Version: v0.8.1-release
API Version: v0.8.0
ABI Version: v0.8
Install root: /opt/homebrew
Search path:  /opt/homebrew/lib/SoapySDR/modules0.8
Module found: /opt/homebrew/lib/SoapySDR/modules0.8/librtlsdrSupport.so     (0.3.3)
Module found: /opt/homebrew/lib/SoapySDR/modules0.8/soapyMiriSupport-dev.so (48e3b31)
```


## Dependencies

### Libmirisdr
This driver works with the [**libmirisdr-5**](https://github.com/ericek111/libmirisdr-5) library -- an open-source version reverse-engineered by Miroslav Slugen.

It is much more stable than the proprietary SDRplay API (that presents a serious security risk -- it needs to run under root). It doesn't need to run in the background, doesn't need to be restarted and thanks to LibUSB, it can work even on Android devices inside chroot.

### SoapySDR

For this to compile and run, you need to have [Pothosware/SoapySDR](https://github.com/pothosware/SoapySDR) installed.

## Usage

### Gains
There are 5 individual gains. While it may be confusing, the method of operation is pretty straightforward -- trial and error. :)

One of them is called "Automatic". This uses an algorithm to configure all the other gains to hopefully provide a linear and easily understandable gain control.

However, if this is not enough for you and you really want to get the best performance (signal to noise ratio, image rejection) out of your SDR, you can control the individual amplification stages inside your SDR using the 4 other gains (LNA, Mixer, Mixbuffer and Baseband).