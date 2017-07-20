# apapi

This is an asynchronous plugin for sampling PAPI performance counters.

## Compilation and Installation

### Prerequisites

To compile this plugin, you need:

* GCC compiler (with `std=11` support)

* `libpthread`

* CMake

* PAPI (`5.2+`)

* Score-P or VampirTrace (`5.14+`)

### Building

1. Create a build directory

        mkdir build
        cd build

2. Invoke CMake

    Specify the VampirTrace and/or Score-P and PAPI directory if it is not in the default path with
    `-DVT_DIR=<PATH>` and/or `-DSCOREP_DIR=<PATH>` respectivly `-DPAPI_INC=<PATH>`, `-DPAPI_LIB=<PATH>` . The plugin will
    use alternatively the environment variables `VT_DIR`, `SCOREP_DIR`, `PAPI_INC` and `PAPI_LIB`, e.g.

        cmake .. -DSCOREP_DIR=/opt/scorep -DPAPI_INC=/opt/papi/include -DPAPI_LIB=/opt/papi/lib

3. Invoke make

        make

4. Copy it to a location listed in `LD_LIBRARY_PATH` or add current path to `LD_LIBRARY_PATH` with

        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:`pwd`

## Usage

### Score-P

To use this plugin, add it to the `SCOREP_METRIC_PLUGINS` environment variable, e.g.:

    export SCOREP_METRIC_PLUGINS="apapi_plugin"

All avaible papi counter should be avaible in this plugin. To use them, simply set the
desired PAPI counters in the `SCOREP_METRIC_APAPI_PLUGIN` environment variable.

    export SCOREP_METRIC_APAPI_PLUGIN="PAPI_L2_TCM,PAPI_FP_INS"

### VampirTrace

All avaible papi counter should be avaible in this plugin. To use them, simply set the
`VT_PLUGIN_CNTR_METRICS` environment variable. Prefix the PAPI counter name with A, e.g.

    export VT_PLUGIN_CNTR_METRICS="apapi_plugin_L2_TCM:apapi_plugin_FP_INS"

### Environment variables

* `SCOREP_METRIC_APAPI_INTERVAL_US`/`VT_APAPI_INTERVAL_US` (default=100000)

    Specifies the interval in usecs, at wich the performance counters are read.
    A higher interval means less disturbance, a lower interval provides more resolution.

* `SCOREP_METRIC_APAPI_BUF_SIZE`/`VT_APAPI_BUF_SIZE` (default=4M)

    The size of the buffer for storing samples. Can be suffixed with G, M, and K.

    The buffer size is per thread, e.g., on a system with two papi counters, 12 threads and 4 MB
    buffer size this would be 48 MB in total. Typically, a sample consists of a 8 byte timestamp and
    8 byte per selected counter.

    If the buffer is too small, it might not be capable of storing all events. If this is the case,
    then a error message will be printed to `stderr`.

### If anything fails

1. Check whether the plugin library can be loaded from the `LD_LIBRARY_PATH`.

2. Write a mail to the author.

## Authors

* Michael Werner (michael.werner3 at tu-dresden dot de)
