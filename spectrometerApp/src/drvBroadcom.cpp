
/*==============================================================
#  Abs: C++ header for Broadcom USB spectrometer driver, using the NioLink API.
#
#  Name: drvBroadcom.cpp
#
#  Desc: This file should include drvBroadcom.h
#
#  Facility: FACET
#
#  Auth: 1-Jul-2026, M. Dunning (mdunning)
===============================================================*/

#include <cstdlib>
#include <cstring>
#include <cmath>

#include <epicsThread.h>
#include <epicsExport.h>
#include <epicsEvent.h>
#include <epicsExit.h>
#include <epicsTime.h>
#ifdef EVR_SUPPORT
#include <evrTime.h>
#endif
#include <iocsh.h>
#include <alarm.h>
#include <errlog.h>
#include <asynOctetSyncIO.h>

#include "drvBroadcom.h"

namespace {
    const std::string driverName = "drvBroadcom";
    const size_t SPECTRUM_LENGTH_MAX = 1024000;
    const size_t RESP_LEN_MAX = 64;
    const size_t RESP_CODE_SIZE = 4;
    const size_t SPEC_HEADER_SIZE = 48;
    const size_t BYTES_PER_UINT = 4;
    const size_t BYTES_PER_FLOAT = 4;
    const int ERROR_COUNT_MAX = 5;
    const double DEFAULT_POLL_DELAY = 0.075;
    const double DEFAULT_TIMEOUT = 1.0;
    const uint32_t NUM_AVE_MAX = 500;
    enum response_code {RESP_OK, RESP_UNKNOWN, RESP_INVALID_PARAM};
    enum spectrum_status {SPEC_IDLE, SPEC_WAIT_TRIG, SPEC_WAIT_TAKE};
    // Commands -----------------------------------------------------
    // Device commands (message type = 0x0)
    const uint8_t cmd_connect[4] =          {0x00, 0x00, 0x00, 0x00};
    const uint8_t cmd_disconnect[4] =       {0x01, 0x00, 0x00, 0x00};
    const uint8_t cmd_reset[4] =            {0x02, 0x00, 0x00, 0x00};
    const uint8_t cmd_start_exposure[4] =   {0x04, 0x00, 0x00, 0x00};
    // Device parameters (message type = 0x1)
    const uint8_t cmd_get_int_time[4] =     {0x00, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_int_time[4] =     {0x00, 0x11, 0x00, 0x00};
    const uint8_t cmd_min_int_time[4] =     {0x00, 0x12, 0x00, 0x00};
    const uint8_t cmd_max_int_time[4] =     {0x00, 0x13, 0x00, 0x00};
    const uint8_t cmd_typ_int_time[4] =     {0x00, 0x18, 0x00, 0x00};
    const uint8_t cmd_get_num_ave[4] =      {0x01, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_num_ave[4] =      {0x01, 0x11, 0x00, 0x00};
    const uint8_t cmd_min_num_ave[4] =      {0x01, 0x12, 0x00, 0x00};
    const uint8_t cmd_max_num_ave[4] =      {0x01, 0x13, 0x00, 0x00};
    const uint8_t cmd_typ_num_ave[4] =      {0x01, 0x18, 0x00, 0x00};
    const uint8_t cmd_get_proc_steps[4] =   {0x02, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_proc_steps[4] =   {0x02, 0x11, 0x00, 0x00};
    const uint8_t cmd_get_gpio_config[4] =  {0x03, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_gpio_config[4] =  {0x03, 0x11, 0x00, 0x00};
    const uint8_t cmd_get_trig_config[4] =  {0x04, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_trig_config[4] =  {0x04, 0x11, 0x00, 0x00};
    const uint8_t cmd_get_trig_delay[4] =   {0x05, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_trig_delay[4] =   {0x05, 0x11, 0x00, 0x00};
    const uint8_t cmd_min_trig_delay[4] =   {0x05, 0x12, 0x00, 0x00};
    const uint8_t cmd_max_trig_delay[4] =   {0x05, 0x13, 0x00, 0x00};
    const uint8_t cmd_get_trig_mode[4] =    {0x06, 0x10, 0x00, 0x00};
    const uint8_t cmd_set_trig_mode[4] =    {0x06, 0x11, 0x00, 0x00};
    const uint8_t cmd_typ_trig_mode[4] =    {0x06, 0x18, 0x00, 0x00};
    // Device properties (message type = 0x2)
    const uint8_t cmd_get_device_id[4] =    {0x00, 0x20, 0x00, 0x00};
    const uint8_t cmd_get_serial_num[4] =   {0x01, 0x20, 0x00, 0x00};
    const uint8_t cmd_get_manufacturer[4] = {0x02, 0x20, 0x00, 0x00};
    const uint8_t cmd_get_model[4] =        {0x03, 0x20, 0x00, 0x00};
    const uint8_t cmd_get_hw_ver[4] =       {0x04, 0x20, 0x00, 0x00};
    const uint8_t cmd_get_fw_ver[4] =       {0x05, 0x20, 0x00, 0x00};
    const uint8_t cmd_get_pixel_cnt[4] =    {0x07, 0x20, 0x00, 0x00};
    // Measured values (message type = 0x3)
    const uint8_t cmd_get_spec_status[4] =  {0x00, 0x30, 0x00, 0x00};
    const uint8_t cmd_get_temp[4] =         {0x01, 0x30, 0x00, 0x00};
    // Bulk data (message type = 0x4)
    const uint8_t cmd_get_spectrum[4] =     {0x00, 0x40, 0x00, 0x00};
    const uint8_t cmd_get_wavelengths[4] =  {0x01, 0x40, 0x00, 0x00};
}

static void pollerThreadC(void* pPvt) {
    drvBroadcom *pdrvBroadcom = (drvBroadcom*)pPvt;
    pdrvBroadcom->pollerThread();
}


/* Constructor for the drvBroadcom class */
drvBroadcom::drvBroadcom(const char *port, const char* ioPort):
        asynPortDriver(port, 1,
                    asynInt32Mask | asynUInt32DigitalMask | asynFloat32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynUInt32DigitalMask | asynFloat32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynOctetMask,  /* Interrupt mask */
                    ASYN_CANBLOCK, /* asynFlags. This driver blocks and it is not multi-device */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0), /* Default stack size*/
                    _running(true),
                    _exited(false),
                    _error_count(0),
                    _event_id(epicsEventCreate(epicsEventEmpty)),
                    _spectrum_length(0),
                    _update_time(DEFAULT_POLL_DELAY),
                    _min_update_time(0.001),
                    _port_name(port),
                    _io_port_name(ioPort),
                    _serial_num(""),
                    _connected(false)
{
    const std::string functionName = "drvBroadcom";
    
    // Asyn parameter table
    createParam(acquireString,             asynParamInt32,         &P_acquire);
    createParam(updateTimeString,          asynParamFloat64,       &P_updateTime);
    createParam(updateTimeActString,       asynParamFloat64,       &P_updateTimeAct);
    createParam(updateRateActString,       asynParamFloat64,       &P_updateRateAct);
    createParam(hwVersionString,           asynParamInt32,         &P_hwVersion);
    createParam(fwVersionString,           asynParamInt32,         &P_fwVersion);
    createParam(deviceIDString,            asynParamInt32,         &P_deviceID);
    createParam(serialNumString,           asynParamOctet,         &P_serialNum);
    createParam(manufacturerString,        asynParamOctet,         &P_manufacturer);
    createParam(modelString,               asynParamOctet,         &P_model);
    createParam(wavelengthsString,         asynParamFloat32Array,  &P_wavelengths);
    createParam(spectrumString,            asynParamFloat32Array,  &P_spectrum);
    createParam(spectrumLengthString,      asynParamInt32,         &P_spectrumLength);
    createParam(spectrumStatusString,      asynParamInt32,         &P_spectrumStatus);
    createParam(minIntegrationTimeString,  asynParamFloat64,       &P_minIntegrationTime);
    createParam(integrationTimeString,     asynParamFloat64,       &P_integrationTime);
    createParam(shutterString,             asynParamInt32,         &P_shutter);
    createParam(numAveString,              asynParamInt32,         &P_numAve);
    createParam(smoothingWidthString,      asynParamInt32,         &P_smoothingWidth);
    createParam(subtractBkgString,         asynParamInt32,         &P_subtractBkg);
    createParam(getBkgString,              asynParamInt32,         &P_getBkg);
    createParam(clearBkgString,            asynParamInt32,         &P_clearBkg);
    createParam(connStatusString,          asynParamInt32,         &P_conn);
    createParam(reconnectString,           asynParamInt32,         &P_reconn);
    createParam(resetString,               asynParamInt32,         &P_reset);
    createParam(trigModeString,            asynParamInt32,         &P_trigMode);
    createParam(trigDelayString,           asynParamInt32,         &P_trigDelay);
    createParam(trigInputString,           asynParamInt32,         &P_trigInput);
    createParam(trigEdgeString,            asynParamInt32,         &P_trigEdge);
    createParam(trigConf1String,           asynParamInt32,         &P_trigConf1);
    createParam(trigConf2String,           asynParamInt32,         &P_trigConf2);
    createParam(tempString,                asynParamFloat64,       &P_temp);
    createParam(procStepsString,           asynParamUInt32Digital, &P_procSteps);
    createParam(gpio0ConfigString,         asynParamInt32,         &P_gpio0Config);
    createParam(gpio1ConfigString,         asynParamInt32,         &P_gpio1Config);
    createParam(gpio2ConfigString,         asynParamInt32,         &P_gpio2Config);
    createParam(gpio3ConfigString,         asynParamInt32,         &P_gpio3Config);
    createParam(eventCodeString,           asynParamInt32,         &P_eventCode);
    createParam(getSpectrumString,         asynParamInt32,         &P_getSpectrum);

    // Start the main thread
    epicsThreadId tid = epicsThreadCreate("drvBroadcomMain",
                    epicsThreadPriorityMedium,
                    epicsThreadGetStackSize(epicsThreadStackMedium),
                    (EPICSTHREADFUNC)pollerThreadC, this);

    if (!tid) {
        errlogPrintf("%s::%s: [%s] epicsThreadCreate failure\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
        return;
    }

    // Create an EPICS exit handler
    epicsAtExit(exitHandler, (void*)this);
}

drvBroadcom::~drvBroadcom() {
/*-----------------------------------------------------------
    Destructor.
--------------------------------------------------------------- */
    const std::string functionName = "~drvBroadcom";
  
    lock();
    _running = false;
    epicsEventSignal(_event_id);
    unlock();
    
    while(!_exited) {
        epicsThreadSleep(0.2);
    }

    // Close device connection & clean up
    lock();
    if (_connected) {
        _disconnect();
    }
    unlock();

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Exiting...\n", driverName.c_str(), functionName.c_str(), _port_name.c_str());
}

void drvBroadcom::pollerThread() {
/*-------------------------------------------------------------------- 
    This function runs in a separate thread.  
----------------------------------------------------------------------*/
    std::string functionName = "pollerThread";
    int num_params = 0, acquire = 0;
    long poll_count = 0;
    epicsTimeStamp ts0, ts1, ts2;
    double tdiff = 0.0, tdiff_cycle, poll_delay = DEFAULT_POLL_DELAY;
    asynStatus status = asynSuccess;

    status = pasynOctetSyncIO->connect(_io_port_name.c_str(), 0, &pasynUser, 0);
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] Failed to connect to I/O port %s\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), _io_port_name.c_str());
    }
    
    status = getNumParams(&num_params);
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] Failed to get number of parameters\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }

    lock();
    // Set some initial conditions
    setIntegerParam(P_acquire, acquire);
    setDoubleParam(P_updateTime, poll_delay);
    setDoubleParam(P_updateTimeAct, 0.0);
    setDoubleParam(P_updateRateAct, 0.0);
    setIntegerParam(P_spectrumStatus, 0);
    setIntegerParam(P_subtractBkg, 0);
    setIntegerParam(P_eventCode, 0);
    callParamCallbacks();

    // Initialize spectrometer
    _connect();

    unlock();

    while(_running) {
        lock();
        epicsTimeGetCurrent(&ts0);

        // Update the poll delay time based on the actual time required to read data
        if (tdiff >= _update_time) {
            poll_delay = 0.0;
        } else {
            poll_delay = _update_time - tdiff;
        }

        getIntegerParam(P_acquire, &acquire);
        // Release the lock while we wait for a command to start or wait for updateTime
        unlock();
        if (acquire) {
            epicsEventWaitWithTimeout(_event_id, poll_delay);
        } else {
            epicsEventWait(_event_id);
        }
        if (!_running) break;
        // Take the lock again
        lock(); 
        // acquire could have changed while we were waiting
        getIntegerParam(P_acquire, &acquire);
        if (!acquire) {
            unlock();
            continue;
        }

        // If not connected, set alarms and try to reconnect periodically
        if (!_connected) {
            setIntegerParam(P_conn, _connected);
            callParamCallbacks();
            for (int i=0; i<num_params; i++) {
                if (i == P_conn) continue;
                setParamAlarmStatus(i, COMM_ALARM);
                setParamAlarmSeverity(i, INVALID_ALARM);
            }
            if (poll_count % 100 == 0) {
                _connect();
            }
        }

        // Poll device and measure elapsed time
        epicsTimeGetCurrent(&ts1);
        if (_connected) {
            _get_spectrum();
            if (poll_count % 1000 == 0) {
                // Poll these less frequently
                _get_temp();
            }
        }
        epicsTimeGetCurrent(&ts2);
        tdiff = epicsTimeDiffInSeconds(&ts2, &ts1);
        tdiff_cycle = epicsTimeDiffInSeconds(&ts2, &ts0);
        double rate = (tdiff_cycle < 0.00001) ? 0.0 : (1.0/tdiff_cycle);
        setDoubleParam(P_updateTimeAct, tdiff_cycle);
        setDoubleParam(P_updateRateAct, rate);
        callParamCallbacks();
        
        unlock();
        poll_count++;
    } // End of while loop
    
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s::%s: [%s] Main thread exiting...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());
    _exited = true;
}


bool drvBroadcom::_connect() {
/*--------------------------------------------------------------------
    Connect to spectrometer.
----------------------------------------------------------------------*/
    std::string functionName = "_connect";
    uint8_t resp[RESP_LEN_MAX];

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Connecting...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    size_t resp_len = _writeReadDevice(cmd_connect, sizeof(cmd_connect), resp, sizeof(resp));

    if (resp_len > 0) {
        uint8_t return_code = resp[0];
        if (return_code == RESP_OK) {
            _connected = true;
            epicsThreadSleep(0.2);
            _get_device_features();
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] Connected to spectrometer with serial number %s\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), _serial_num.c_str());
        } else {
            _connected = false;
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] ERROR: Return code %d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), return_code);
        }
        setIntegerParam(P_conn, _connected);
        callParamCallbacks();
    }

    return _connected;
}


bool drvBroadcom::_disconnect() {
/*--------------------------------------------------------------------
    Connect to spectrometer.
----------------------------------------------------------------------*/
    std::string functionName = "_disconnect";
    uint8_t resp[RESP_LEN_MAX];

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Disconnecting...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    size_t resp_len = _writeReadDevice(cmd_disconnect, sizeof(cmd_disconnect), resp, sizeof(resp));

    if (resp_len > 0) {
        uint8_t return_code = resp[0];
        if (return_code == RESP_OK) {
            _connected = false;
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] Disconnected\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str());
        } else {
            _connected = true;
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] ERROR: Return code %d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), return_code);
        }
        setIntegerParam(P_conn, _connected);
        callParamCallbacks();
    }

    return _connected;
}


void drvBroadcom::_reset() {
/*--------------------------------------------------------------------
    Reset spectrometer.
----------------------------------------------------------------------*/
    std::string functionName = "_reset";
    uint8_t resp[RESP_LEN_MAX];

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Resetting...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    size_t resp_len = _writeReadDevice(cmd_reset, sizeof(cmd_reset), resp, sizeof(resp));

    if (resp_len > 0) {
        uint8_t return_code = resp[0];
        if (return_code == RESP_OK) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] Reset successful\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str());
        } else {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] ERROR: Return code %d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), return_code);
        }
    }
}


bool drvBroadcom::_get_device_features() {
/*-------------------------------------------------------------------- 
Get spectrometer features and push to records.
----------------------------------------------------------------------*/
    std::string functionName = "_get_device_features";
    std::string resp_str;
    uint32_t resp_uint32;

    // Device ID
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting device ID...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());
    resp_uint32 = _getUInt(cmd_get_device_id, sizeof(cmd_get_device_id));
    setIntegerParam(P_deviceID, resp_uint32);

    // Serial num
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting serial num...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());
    resp_str = _getString(cmd_get_serial_num, sizeof(cmd_get_serial_num));
    _serial_num = resp_str;
    setStringParam(P_serialNum, resp_str);

    // Manufacturer
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting manu...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());
    resp_str = _getString(cmd_get_manufacturer, sizeof(cmd_get_manufacturer));
    setStringParam(P_manufacturer, resp_str);

    // Model
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting model...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());
    resp_str = _getString(cmd_get_model, sizeof(cmd_get_model));
    setStringParam(P_model, resp_str);

    // Hardware version
    resp_uint32 = _getUInt(cmd_get_hw_ver, sizeof(cmd_get_hw_ver));
    setIntegerParam(P_hwVersion, resp_uint32);

    // Firmware version
    resp_uint32 = _getUInt(cmd_get_fw_ver, sizeof(cmd_get_fw_ver));
    setIntegerParam(P_fwVersion, resp_uint32);

    // Pixel count
    _spectrum_length = _getUInt(cmd_get_pixel_cnt, sizeof(cmd_get_pixel_cnt));
    setIntegerParam(P_spectrumLength, _spectrum_length);
    if ((_spectrum_length <= 0) || (_spectrum_length > SPECTRUM_LENGTH_MAX)) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] ERROR: invalid spectrum length %zu\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), _spectrum_length);
        return false;
    }

    _get_int_time();
    _get_num_ave();
    _get_trig_mode();
    _get_trig_delay();
    _get_temp();
    _get_proc_steps();
    _get_trig_config();
    _get_gpio_config();

    // Resize vectors to actual spectrum length 
    _wavelengths.resize(_spectrum_length);
    _spectrum.resize(_spectrum_length);
    _background_spectrum.resize(_spectrum_length);
    std::fill(_background_spectrum.begin(), _background_spectrum.end(), 0.0);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] _wavelengths.size=%zu, _spectrum.size=%zu, _background_spectrum.size=%zu\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), _wavelengths.size(), _spectrum.size(),
                    _background_spectrum.size());

    callParamCallbacks();
    return true;
}


void drvBroadcom::_get_spectrum(bool is_background) {
/*-------------------------------------------------------------------- 
Get wavelengths and spectrum arrays, optionally subtract background, push to records.
----------------------------------------------------------------------*/
    std::string functionName = "_get_spectrum";
    asynStatus status = asynSuccess;
    size_t nbytesOut = 0, nbytesIn = 0, cmd_str_size = 0, resp_size = 0;
    int eomReason, spec_status_resp, spec_status, spec_wait_count = 0, num_spectra = 0;
    uint8_t return_code = 1;
    uint8_t* pdata = NULL;
    std::vector<uint8_t> cmd, resp;
    
    double int_time_ms;
    getDoubleParam(P_integrationTime, &int_time_ms);
    double int_time_sec = int_time_ms/1000.;
    double spec_wait_inc = 0.001;
    int timeout_count = static_cast<int>(std::ceil(25.0*(1.0/spec_wait_inc)));

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting spectrum...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    // Start exposure 
    _setUInt(cmd_start_exposure, sizeof(cmd_start_exposure), 1);
    // Wait for spectrum.  Instead of blocking, they make us do the work.
    // Must check status in a loop.
    while (_running && _connected) {
        spec_status_resp = _getInt(cmd_get_spec_status, sizeof(cmd_get_spec_status));
        // Spectrum exposure status is byte 0.  If SPEC_IDLE, a spectrum should be ready.
        spec_status = spec_status_resp & 0xff;
        if (spec_status == SPEC_IDLE) {
            // Spectrum should be ready; number of avialable spectra is bytes 1 and 2
            num_spectra = (spec_status_resp >> 8) & 0xffff;
            if (num_spectra < 1) {
                asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                        "%s::%s: [%s] Warning: no spectra available (num_spectra=%d)\n",
                        driverName.c_str(), functionName.c_str(), _port_name.c_str(), num_spectra);
                // Note: Don't return here, spectrometer sometimes falsely reports no spectra available. 
            }
            asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s::%s: [%s] Spectrum ready, %d available, spec_wait_count=%d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), num_spectra, spec_wait_count);
            break;
        } else if (spec_status < 0) {
            if (!_error_count) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s::%s: [%s] Error: spectrum status = %d\n",
                        driverName.c_str(), functionName.c_str(), _port_name.c_str(), spec_status);
            }
            _error_count++;
            return;
        } else {
            asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s::%s: [%s] Spectrum not ready, spectrum_status=%d, spec_wait_count=%d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), spec_status, spec_wait_count);
            epicsThreadSleep(spec_wait_inc);
            spec_wait_count++;
            if (int_time_sec > 0.25) {
                // This is only useful with a long integration time
                setIntegerParam(P_spectrumStatus, spec_status);
                callParamCallbacks();
            }
            // Time out if we wait too long
            if (spec_wait_count > timeout_count) {
                if (!_error_count) {
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                            "%s::%s: [%s] Timed out waiting for spectrum, spec_wait_count=%d\n",
                            driverName.c_str(), functionName.c_str(), _port_name.c_str(), spec_wait_count);
                }
                _error_count++;
                return;
            }
        }
    }

    // Spectrum must be ready, get it
    cmd_str_size = sizeof(cmd_get_spectrum);
    cmd.resize(cmd_str_size);
    std::memcpy(cmd.data(), cmd_get_spectrum, cmd_str_size);
    resp_size = RESP_CODE_SIZE + SPEC_HEADER_SIZE + _spectrum_length*BYTES_PER_FLOAT;
    resp.resize(resp_size);
    status = pasynOctetSyncIO->writeRead(pasynUser, reinterpret_cast<char*>(cmd.data()), cmd_str_size,
            reinterpret_cast<char*>(resp.data()), resp_size, DEFAULT_TIMEOUT, &nbytesOut, &nbytesIn, &eomReason);

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Sent message: status=%d, nbytesOut=%zu, nbytesIn=%zu, eomReason=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), status, nbytesOut, nbytesIn, eomReason);

    return_code = resp[0];
    if ((status != asynSuccess) || (nbytesIn < resp_size) || (return_code != RESP_OK)) {
        // Print an error message if this is the first error, otherwise just keep count
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] REPLY ERROR: status=%d, nbytesIn=%zu, resp_size=%zu, return_code=%d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                    status, nbytesIn, resp_size, return_code);
        }
        _error_count++;
        return;
    }

    // Unpack header
    std::vector<uint8_t> spec_header(resp.begin() + RESP_CODE_SIZE, resp.begin() + (RESP_CODE_SIZE + SPEC_HEADER_SIZE));
    uint16_t spec_len = _unpack_spectrum_header(spec_header);

    if ((spec_len <= 0) || (spec_len != _spectrum_length)) {
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] Error getting spectrum, desired length=%zu, actual length=%d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), _spectrum_length, spec_len);
        }
        _error_count++;
        return;
    }


    // Fill spectrum vector
    pdata = resp.data() + (RESP_CODE_SIZE + SPEC_HEADER_SIZE);
    if (!pdata) {
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] pdata is NULL\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str());
        }
        _error_count++;
        return;
    }
    if (is_background) {
        // If this is a background spectrum, just store it
        std::memcpy(_background_spectrum.data(), pdata, _spectrum_length*BYTES_PER_FLOAT);
        return;
    }
    std::memcpy(_spectrum.data(), pdata, _spectrum_length*BYTES_PER_FLOAT);

#ifdef EVR_SUPPORT
    epicsTimeStamp evr_timestamp;
    int event_code = 0;
    getIntegerParam(P_eventCode, &event_code);
    if (evrTimeGet(&evr_timestamp, event_code) == 0) {
        if (setTimeStamp(&evr_timestamp) != asynSuccess) {
            asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s::%s: [%s] Error in setTimeStamp()\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str());
        }
    }
#endif

    // Subtract background
    int subtract_background;
    getIntegerParam(P_subtractBkg, &subtract_background);
    if (subtract_background) {
        if ((_spectrum.size() == _spectrum_length) && (_background_spectrum.size() == _spectrum_length)) {
            for (size_t i=0; i<_spectrum_length; i++) {
                _spectrum[i] -= _background_spectrum[i];
                if (_spectrum[i] < 0.0) {
                    _spectrum[i] = 0.0;
                }
            }
        } else {
            asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s::%s: [%s] Error in spectrum size: _spectrum.size()=%zu, _background_spectrum.size()=%zu,\
                    _spectrum_length=%zu\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                    _spectrum.size(), _background_spectrum.size(), _spectrum_length);
        }
    }


    status = doCallbacksFloat32Array(_spectrum.data(), _spectrum_length, P_spectrum, 0);
    if (status != asynSuccess) {
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] Error in spectrum callback\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str());
        }
        _error_count++;
        return;
    }



    // Get wavelengths
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting wavelengths...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    cmd_str_size = sizeof(cmd_get_wavelengths);
    cmd.resize(cmd_str_size);
    std::memcpy(cmd.data(), cmd_get_wavelengths, cmd_str_size);
    resp_size = RESP_CODE_SIZE + _spectrum_length*BYTES_PER_FLOAT;
    resp.resize(resp_size);
    status = pasynOctetSyncIO->writeRead(pasynUser, reinterpret_cast<char*>(cmd.data()), cmd_str_size,
            reinterpret_cast<char*>(resp.data()), resp_size, DEFAULT_TIMEOUT, &nbytesOut, &nbytesIn, &eomReason);

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Sent message: status=%d, nbytesOut=%zu, nbytesIn=%zu, eomReason=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), status, nbytesOut, nbytesIn, eomReason);

    return_code = resp[0];
    if ((status != asynSuccess) || (nbytesIn < resp_size) || (return_code != RESP_OK)) {
        // Print an error message if this is the first error, otherwise just keep count
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] REPLY ERROR: status=%d, nbytesIn=%zu, resp_size=%zu, return_code=%d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                    status, nbytesIn, resp_size, return_code);
        }
        _error_count++;
        return;
    }

    // Fill wavelengths vector
    pdata = resp.data() + (RESP_CODE_SIZE); 
    if (!pdata) {
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] pdata is NULL\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str());
        }
        _error_count++;
        return;
    }
    std::memcpy(_wavelengths.data(), pdata, _spectrum_length*BYTES_PER_FLOAT);

    status = doCallbacksFloat32Array(_wavelengths.data(), _spectrum_length, P_wavelengths, 0);
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Error in wavelengths callback\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }

    // If we've had an error, clear error count and print a diagnostic message
    if (_error_count) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] Device %s OK after %d errors\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                _serial_num.c_str(), _error_count);
        _error_count = 0;
    }

}


uint16_t drvBroadcom::_unpack_spectrum_header(const std::vector<uint8_t> header) {
/*-------------------------------------------------------------------- 
----------------------------------------------------------------------*/
    std::string functionName = "_unpack_spectrum_header";

    const uint8_t* pdata = header.data();

    uint32_t int_time_act = _bytes_to_uint(pdata + 0);
    int32_t num_ave = _bytes_to_int(pdata + 4);
    uint32_t timestamp = _bytes_to_uint(pdata + 8);
    double load_level = _bytes_to_float(pdata + 12);
    double temperature = _bytes_to_float(pdata + 16);
    uint16_t spec_len = _bytes_to_ushort(pdata + 20);
    uint16_t pixel_format = _bytes_to_ushort(pdata + 22);
    uint16_t proc_steps = _bytes_to_ushort(pdata + 24);
    uint16_t units = _bytes_to_ushort(pdata + 26);
    int32_t num_dropped = _bytes_to_int(pdata + 28);
    double saturation_val = _bytes_to_float(pdata + 32);
    double offset_ave = _bytes_to_float(pdata + 36);
    double dark_ave = _bytes_to_float(pdata + 40);
    double readout_noise = _bytes_to_float(pdata + 44);
    
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] int_time=%d, num_ave=%d, timestamp=%d, load_level=%f, temperature=%f,\
            spec_len=%d, pixel_format=%d, proc_steps=%d, units=%d, num_dropped=%d,\
            saturation_val=%f, offset_ave=%f, dark_ave=%f, readout_noise=%f\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), int_time_act, num_ave, timestamp,
            load_level, temperature, spec_len, pixel_format, proc_steps, units, 
            num_dropped, saturation_val, offset_ave, dark_ave, readout_noise);

    return spec_len;
}


void drvBroadcom::_get_int_time() {
/*-------------------------------------------------------------------- 
    Get integration time, push value to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_int_time";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting int time...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t int_time_us = _getUInt(cmd_get_int_time, sizeof(cmd_get_int_time));
    double int_time_ms = static_cast<double>(int_time_us)/1000.0;
    uint32_t int_time_min_us = _getUInt(cmd_min_int_time, sizeof(cmd_min_int_time));
    double int_time_min_ms = static_cast<double>(int_time_min_us)/1000.0;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] int_time_us=%d (%f ms), int_time_min_us=%d (%f ms)\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), int_time_us, int_time_ms, 
            int_time_min_us, int_time_min_ms);

    setDoubleParam(P_integrationTime, int_time_ms);
    setDoubleParam(P_minIntegrationTime, int_time_min_ms);
    callParamCallbacks();
}


void drvBroadcom::_set_int_time(epicsFloat64 value) {
/*-------------------------------------------------------------------- 
    Set integration time. Validate, verify setting.
    User units are ms (double), convert to us (uint32_t) before sending.
----------------------------------------------------------------------*/
    std::string functionName = "_set_int_time";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting int time = %f...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    uint32_t int_time_us = static_cast<uint32_t>(value*1000);
    uint32_t int_time_min_us = _getUInt(cmd_min_int_time, sizeof(cmd_min_int_time));
    uint32_t int_time_max_us = _getUInt(cmd_max_int_time, sizeof(cmd_max_int_time));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Int time min=%d us, max=%d us\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), int_time_min_us, int_time_max_us);

    if (int_time_us < int_time_min_us) {
        int_time_us = int_time_min_us;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s: [%s] Warning: integration time too small, changed from %f to %f\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(),
            value, static_cast<epicsFloat64>(int_time_us/1000.0));
        setDoubleParam(P_integrationTime, static_cast<epicsFloat64>(int_time_us/1000.0));
        callParamCallbacks();
    }

    if (int_time_us > int_time_max_us) {
        int_time_us = int_time_max_us;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s: [%s] Warning: integration time too large, changed from %f to %f\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(),
            value, static_cast<epicsFloat64>(int_time_us/1000.0));
        setDoubleParam(P_integrationTime, static_cast<epicsFloat64>(int_time_us/1000.0));
        callParamCallbacks();
    }

    _setUInt(cmd_set_int_time, sizeof(cmd_set_int_time), int_time_us);

    if (_connected) {
        epicsThreadSleep(0.1);
        _get_int_time();
    }
}


void drvBroadcom::_get_trig_mode() {
/*-------------------------------------------------------------------- 
    Get trigger mode, push value to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_trig_mode";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting trig mode...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t value = _getUInt(cmd_get_trig_mode, sizeof(cmd_get_trig_mode));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] trig_mode=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    setIntegerParam(P_trigMode, value);
    callParamCallbacks();
}


void drvBroadcom::_set_trig_mode(uint32_t value) {
/*-------------------------------------------------------------------- 
    Validate & set trig mode.
----------------------------------------------------------------------*/
    std::string functionName = "_set_trig_mode";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting I/O config...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    _setUInt(cmd_set_trig_mode, sizeof(cmd_set_trig_mode), value);

    if (_connected) {
        epicsThreadSleep(0.1);
        _get_trig_mode();
    }
}


void drvBroadcom::_get_trig_delay() {
/*-------------------------------------------------------------------- 
    Get integration time, push value to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_trig_delay";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting trig delay...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t value = _getUInt(cmd_get_trig_delay, sizeof(cmd_get_trig_delay));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] trig_delay=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    setIntegerParam(P_trigDelay, value);
    callParamCallbacks();
}


void drvBroadcom::_set_trig_delay(uint32_t value) {
/*-------------------------------------------------------------------- 
    Set trig delay. Validate, verify setting.
    User units are us (uint32_t) before sending.
----------------------------------------------------------------------*/
    std::string functionName = "_set_trig_delay";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting trig delay = %d...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    uint32_t trig_delay = value;
    uint32_t trig_delay_min = _getUInt(cmd_min_trig_delay, sizeof(cmd_min_trig_delay));
    uint32_t trig_delay_max = _getUInt(cmd_max_trig_delay, sizeof(cmd_max_trig_delay));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Trig delay min=%d us, max=%d us\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), trig_delay_min, trig_delay_max);

    if (trig_delay < trig_delay_min) {
        trig_delay = trig_delay_min;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s: [%s] Warning: trig delay too small, changed from %d to %d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, trig_delay);
        setIntegerParam(P_trigDelay, trig_delay);
        callParamCallbacks();
    }

    if (trig_delay > trig_delay_max) {
        trig_delay = trig_delay_max;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s: [%s] Warning: trig delay too large, changed from %d to %d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, trig_delay);
        setDoubleParam(P_trigDelay, trig_delay);
        callParamCallbacks();
    }

    _setUInt(cmd_set_trig_delay, sizeof(cmd_set_trig_delay), trig_delay);

    if (_connected) {
        epicsThreadSleep(0.1);
        _get_trig_delay();
    }
}


void drvBroadcom::_get_trig_config() {
/*-------------------------------------------------------------------- 
    Get trigger config, push value(s) to parameter library.
    Byte 0 is the "mode" with:
        Bit0 = Edge
        Bit1 = Conf1 (End/start of exposure)
        Bit2 = Conf2 (Normal or low-jitter)
    Byte 1 is the GPIO input (0-3). See datasheet for mapping to physical pins.
----------------------------------------------------------------------*/
    std::string functionName = "_get_trig_config";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting trig config...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t value = _getUInt(cmd_get_trig_config, sizeof(cmd_get_trig_config));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] trig_config=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    uint8_t bytes[4] = {0};
    _uint_to_bytes(value, bytes);
    setIntegerParam(P_trigEdge,  static_cast<int>((bytes[0] >> 0) & 0x01));
    setIntegerParam(P_trigConf1, static_cast<int>((bytes[0] >> 1) & 0x01));
    setIntegerParam(P_trigConf2, static_cast<int>((bytes[0] >> 2) & 0x01));
    setIntegerParam(P_trigInput, static_cast<int>(bytes[1]));
    callParamCallbacks();
}


void drvBroadcom::_set_trig_config(int function, epicsInt32 value) {
/*-------------------------------------------------------------------- 
    Set trigger config.
    Byte 0 is the "mode" with:
        Bit0 = Edge
        Bit1 = Conf1 (End/start of exposure)
        Bit2 = Conf2 (Normal or low-jitter)
    Byte 1 is the GPIO input (0-3). See datasheet for mapping to physical pins.
----------------------------------------------------------------------*/
    std::string functionName = "_set_trig_config";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting trig config...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    epicsInt32 edge, conf1, conf2, input;
    getIntegerParam(P_trigEdge, &edge);
    getIntegerParam(P_trigConf1, &conf1);
    getIntegerParam(P_trigConf2, &conf2);
    getIntegerParam(P_trigInput, &input);

    if (function == P_trigEdge) {
        edge = value;
    } else if (function == P_trigConf1) {
        conf1 = value;
    } else if (function == P_trigConf2) {
        conf2 = value;
    } else if (function == P_trigInput) {
        input = value;
    }

    uint8_t bytes[4] = {0};
    bytes[0] = static_cast<uint8_t>(((edge & 1) | ((conf1 & 1) << 1) | ((conf2 & 1) << 2)) & 0xff);
    bytes[1] = static_cast<uint8_t>(input & 0xff);

    uint32_t val = _bytes_to_uint(bytes);

    _setUInt(cmd_set_trig_config, sizeof(cmd_set_trig_config), val);
    
    if (_connected) {
        epicsThreadSleep(0.1);
        _get_trig_config();
    }
}


void drvBroadcom::_get_gpio_config() {
/*-------------------------------------------------------------------- 
    Get GPIO config, push value to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_gpio_config";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting I/O config...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t value = _getUInt(cmd_get_gpio_config, sizeof(cmd_get_gpio_config));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] gpio_config=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);
    
    uint8_t bytes[4] = {0};
    _uint_to_bytes(value, bytes);

    setIntegerParam(P_gpio0Config, static_cast<int>(bytes[0]));
    setIntegerParam(P_gpio1Config, static_cast<int>(bytes[1]));
    setIntegerParam(P_gpio2Config, static_cast<int>(bytes[2]));
    setIntegerParam(P_gpio3Config, static_cast<int>(bytes[3]));
    callParamCallbacks();
}


void drvBroadcom::_set_gpio_config(int function, epicsInt32 value) {
/*-------------------------------------------------------------------- 
    Set GPIO config.
----------------------------------------------------------------------*/
    std::string functionName = "_set_gpio_config";

    if ((value < 0) || (value > UINT8_MAX)) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] ERROR: value %d out of bounds...\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);
        return;
    }

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting I/O config...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    epicsInt32 gpio0, gpio1, gpio2, gpio3;
    getIntegerParam(P_gpio0Config, &gpio0);
    getIntegerParam(P_gpio1Config, &gpio1);
    getIntegerParam(P_gpio2Config, &gpio2);
    getIntegerParam(P_gpio3Config, &gpio3);

    if (function == P_gpio0Config) {
        gpio0 = value;
    } else if (function == P_gpio1Config) {
        gpio1 = value;
    } else if (function == P_gpio2Config) {
        gpio2 = value;
    } else if (function == P_gpio3Config) {
        gpio3 = value;
    }

    uint8_t bytes[4] = {0};
    bytes[0] = static_cast<uint8_t>(gpio0 & 0xff);
    bytes[1] = static_cast<uint8_t>(gpio1 & 0xff);
    bytes[2] = static_cast<uint8_t>(gpio2 & 0xff);
    bytes[3] = static_cast<uint8_t>(gpio3 & 0xff);

    uint32_t val = _bytes_to_uint(bytes);

    _setUInt(cmd_set_gpio_config, sizeof(cmd_set_gpio_config), val);
    
    if (_connected) {
        epicsThreadSleep(0.1);
        _get_gpio_config();
    }
}


void drvBroadcom::_get_num_ave() {
/*-------------------------------------------------------------------- 
    Get num_ave (number of spectra to average), push value to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_num_ave";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting num_ave...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t value = _getUInt(cmd_get_num_ave, sizeof(cmd_get_num_ave));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] num_ave=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    setIntegerParam(P_numAve, value);
    callParamCallbacks();
}


void drvBroadcom::_set_num_ave(uint32_t value) {
/*-------------------------------------------------------------------- 
    Validate & set number of spectra to average.
----------------------------------------------------------------------*/
    std::string functionName = "_set_num_ave";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting num ave = %d...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    uint32_t num_ave = value;
    uint32_t num_ave_min = _getUInt(cmd_min_num_ave, sizeof(cmd_min_num_ave));
    uint32_t num_ave_max = _getUInt(cmd_max_num_ave, sizeof(cmd_max_num_ave));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Num ave min=%d, max=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), num_ave_min, num_ave_max);

    if (num_ave < num_ave_min) {
        num_ave = num_ave_min;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s: [%s] Warning: num averages too small, changed from %d to %d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, num_ave);
        setIntegerParam(P_numAve, num_ave);
        callParamCallbacks();
    }

    if (num_ave > num_ave_max) {
        num_ave = num_ave_max;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s: [%s] Warning: num averages too large, changed from %d to %d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, num_ave);
        setIntegerParam(P_numAve, num_ave);
        callParamCallbacks();
    }

    _setUInt(cmd_set_num_ave, sizeof(cmd_set_num_ave), num_ave);

    if (_connected) {
        epicsThreadSleep(0.1);
        _get_num_ave();
    }
}


void drvBroadcom::_get_temp() {
/*-------------------------------------------------------------------- 
    Get temperature, push value to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_temp";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting temp...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    double value = _getFloat(cmd_get_temp, sizeof(cmd_get_temp));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] temp=%f\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    setDoubleParam(P_temp, value);
    callParamCallbacks();
}


void drvBroadcom::_get_proc_steps() {
/*-------------------------------------------------------------------- 
    Get processing steps, push value(s) to parameter library.
----------------------------------------------------------------------*/
    std::string functionName = "_get_proc_steps";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Getting proc steps...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str());

    uint32_t value = _getUInt(cmd_get_proc_steps, sizeof(cmd_get_proc_steps));
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] proc_steps=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    setUIntDigitalParam(P_procSteps, value, 0xffffffff);
    callParamCallbacks();
}


void drvBroadcom::_set_proc_steps(uint32_t value) {
/*-------------------------------------------------------------------- 
    Validate & set spectrum processing steps.
----------------------------------------------------------------------*/
    std::string functionName = "_set_proc_steps";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Setting proc steps = %d...\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);

    _setUInt(cmd_set_proc_steps, sizeof(cmd_set_proc_steps), value);

    if (_connected) {
        epicsThreadSleep(0.1);
        _get_proc_steps();
    }
}


size_t drvBroadcom::_writeReadDevice(const uint8_t* cmd_str, size_t cmd_str_size, uint8_t* resp, size_t resp_size) {
/*--------------------------------------------------------------------
    Send command to device, read response.
----------------------------------------------------------------------*/
    std::string functionName = "_writeReadDevice";
    asynStatus status = asynSuccess;
    size_t nbytesOut = 0, nbytesIn = 0;
    int eomReason;
    char cmd[cmd_str_size];

    if (!cmd_str || !cmd_str_size) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] cmd_str buffer invalid\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
        return 0;
    }
    
    if (!resp || !resp_size) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] resp buffer invalid\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
        return 0;
    }

    std::memcpy(cmd, cmd_str, cmd_str_size);

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Sending message: len=%zu\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), cmd_str_size);

    status = pasynOctetSyncIO->writeRead(pasynUser, cmd, cmd_str_size,
            reinterpret_cast<char*>(resp), resp_size, DEFAULT_TIMEOUT, &nbytesOut, &nbytesIn, &eomReason);

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Sent message: status=%d, nbytesOut=%zu, nbytesIn=%zu, eomReason=%d\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), status, nbytesOut, nbytesIn, eomReason);

    uint8_t return_code = resp[0];
    if ((status != asynSuccess) || (nbytesIn == 0) || (return_code != RESP_OK)) {
        if (!_error_count) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] ERROR: Bad response, status=%d, nbytesIn=%zu, response code=%d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                    status, nbytesIn, return_code);
        }
        _error_count++;
        return 0;
    }

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s::%s: [%s] Response OK (%d)\n",
            driverName.c_str(), functionName.c_str(), _port_name.c_str(), return_code);

    // If we've had an error, clear error count and print a diagnostic message
    if (_error_count) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] Device %s OK after %d errors\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                _serial_num.c_str(), _error_count);
        _error_count = 0;
    }

    return nbytesIn;
}


std::string drvBroadcom::_getString(const uint8_t* cmd_str, size_t cmd_str_size) {
/*-------------------------------------------------------------------- 
----------------------------------------------------------------------*/
    std::string functionName = "_getString";
    uint8_t resp[RESP_LEN_MAX];
    std::string resp_str;

    size_t resp_len = _writeReadDevice(cmd_str, cmd_str_size, resp, sizeof(resp));

    if (resp_len > cmd_str_size) {
        resp_str.assign(reinterpret_cast<const char*>(resp + cmd_str_size), (resp_len - cmd_str_size));
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = %s\n\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), resp_str.c_str());
    }

    return resp_str;
}


uint32_t drvBroadcom::_getUInt(const uint8_t* cmd_str, size_t cmd_str_size) {
/*-------------------------------------------------------------------- 
    Send command, return unsigned int.  
    Total response length should be 8 bytes (4 null bytes + 4 data bytes).
----------------------------------------------------------------------*/
    std::string functionName = "_getUInt";
    uint8_t resp[RESP_LEN_MAX];

    size_t resp_len = _writeReadDevice(cmd_str, cmd_str_size, resp, sizeof(resp));

    if (resp_len >= (cmd_str_size + sizeof(uint32_t))) {
        uint32_t value = _bytes_to_uint(resp + cmd_str_size);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = 0x%08x\n\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);
        return value;
    } else {
        return 0;
    }
}


void drvBroadcom::_setUInt(const uint8_t* cmd_str, size_t cmd_str_size, uint32_t value) {
/*-------------------------------------------------------------------- 
----------------------------------------------------------------------*/
    std::string functionName = "_setUInt";
    uint8_t resp[RESP_LEN_MAX];

    uint8_t bytes[BYTES_PER_UINT];
    _uint_to_bytes(value, bytes);

    size_t len_total = cmd_str_size + sizeof(bytes);
    uint8_t buf[len_total];
    std::memcpy(buf, cmd_str, cmd_str_size);
    std::memcpy(buf + cmd_str_size, bytes, sizeof(bytes)); 
    
    size_t resp_len = _writeReadDevice(buf, sizeof(buf), resp, sizeof(resp));
    
    if (resp_len > 0) {
        uint8_t return_code = resp[0];
        if (return_code != RESP_OK) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s::%s: [%s] ERROR: Return code %d\n",
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), return_code);
        }
    }
}


int32_t drvBroadcom::_getInt(const uint8_t* cmd_str, size_t cmd_str_size) {
/*-------------------------------------------------------------------- 
    Send command, return signed int.
    Total response length should be 8 bytes (4 response bytes + 4 data bytes).
----------------------------------------------------------------------*/
    std::string functionName = "_getInt";
    uint8_t resp[RESP_LEN_MAX];

    size_t resp_len = _writeReadDevice(cmd_str, cmd_str_size, resp, sizeof(resp));

    if (resp_len >= (cmd_str_size + sizeof(int32_t))) {
        int32_t value = _bytes_to_int(resp + cmd_str_size);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = 0x%08x\n\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);
        return value;
    } else {
        return 0;
    }
}


double drvBroadcom::_getFloat(const uint8_t* cmd_str, size_t cmd_str_size) {
/*-------------------------------------------------------------------- 
    Send command, return double.  
    Total response length should be 8 bytes (4 null bytes + 4 data bytes).
----------------------------------------------------------------------*/
    std::string functionName = "_getUInt";
    uint8_t resp[RESP_LEN_MAX];

    size_t resp_len = _writeReadDevice(cmd_str, cmd_str_size, resp, sizeof(resp));

    if (resp_len >= (cmd_str_size + BYTES_PER_FLOAT)) {
        double value = _bytes_to_float(resp + cmd_str_size);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = %f\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value);
        return value;
    } else {
        return 0.0;
    }
}


uint32_t drvBroadcom::_bytes_to_uint(const uint8_t* byte_array) {
/*-------------------------------------------------------------------- 
    Convert 4 bytes to unsigned int, little-endian byte order.
----------------------------------------------------------------------*/
    std::string functionName = "_bytes_to_uint";
    uint32_t value = 0;

    if (byte_array) {
        value = (static_cast<std::uint8_t>(byte_array[0]) << 0)  |
                (static_cast<std::uint8_t>(byte_array[1]) << 8)  |
                (static_cast<std::uint8_t>(byte_array[2]) << 16) |
                (static_cast<std::uint8_t>(byte_array[3]) << 24);

        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = 0x%08x (%d)\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, value);
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] byte_array is null\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }

    return value;
}


void drvBroadcom::_uint_to_bytes(uint32_t value, uint8_t* byte_array) {
/*-------------------------------------------------------------------- 
    Convert unsigned int to byte array (4 bytes), little-endian byte order.
----------------------------------------------------------------------*/
    std::string functionName = "_uint_to_bytes";

    if (byte_array) {
        byte_array[0] = (value >>  0) & 0xff;
        byte_array[1] = (value >>  8) & 0xff;
        byte_array[2] = (value >> 16) & 0xff;
        byte_array[3] = (value >> 24) & 0xff;
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] byte_array is null\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }
}


int32_t drvBroadcom::_bytes_to_int(const uint8_t* byte_array) {
/*-------------------------------------------------------------------- 
    Convert 4 bytes to signed int, little-endian byte order.
----------------------------------------------------------------------*/
    std::string functionName = "_bytes_to_int";
    int32_t value = 0;

    if (byte_array) {
        value = (static_cast<std::int8_t>(byte_array[0]) << 0)  |
                (static_cast<std::int8_t>(byte_array[1]) << 8)  |
                (static_cast<std::int8_t>(byte_array[2]) << 16) |
                (static_cast<std::int8_t>(byte_array[3]) << 24);

        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = 0x%08x (%d)\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, value);
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] byte_array is null\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }

    return value;
}


double drvBroadcom::_bytes_to_float(const uint8_t* byte_array) {
/*-------------------------------------------------------------------- 
    Convert 4 bytes to float, then to double.
----------------------------------------------------------------------*/
    std::string functionName = "_bytes_to_float";
    float float_value = 0.0;
    double value = 0.0;

    if (byte_array) {
        std::memcpy(&float_value, byte_array, BYTES_PER_FLOAT);
        value = static_cast<double>(float_value);
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] byte_array is null\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }

    return value;
}


uint16_t drvBroadcom::_bytes_to_ushort(const uint8_t* byte_array) {
/*-------------------------------------------------------------------- 
    Convert 2 bytes to unsigned short, little-endian byte order.
----------------------------------------------------------------------*/
    std::string functionName = "_bytes_to_ushort";
    uint16_t value = 0;

    if (byte_array) {
        value = (static_cast<std::uint8_t>(byte_array[0]) << 0) |
                (static_cast<std::uint8_t>(byte_array[1]) << 8);

        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] Value = 0x%04x (%d)\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, value);
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s::%s: [%s] byte_array is null\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str());
    }

    return value;
}


asynStatus drvBroadcom::writeInt32(asynUser *pasynUser, epicsInt32 value) {
/*-------------------------------------------------------------------- 
    asynPortDriver override.
----------------------------------------------------------------------*/
    std::string functionName = "writeInt32";
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName;

    /* Set the parameter in the parameter library. */
    status = (asynStatus)setIntegerParam(function, value);
    
    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_acquire) {
        /* If acquire was set then wake up the acquisition task */
        if (value) {
            epicsEventSignal(_event_id);
        } else {
            setDoubleParam(P_updateTimeAct, 0.0);
            setDoubleParam(P_updateRateAct, 0.0);
            setIntegerParam(P_spectrumStatus, SPEC_IDLE);
        }
    } else if (function == P_reconn) {
        if (value) {
            _disconnect();
            epicsThreadSleep(0.2);
            _connect();
            epicsEventSignal(_event_id);
        }
    } else if (function == P_reset) {
        if (value) {
            _reset();
            epicsEventSignal(_event_id);
        }
    } else if (function == P_getBkg) {
        if (_connected) {
            _get_spectrum(true);
        }
    } else if (function == P_clearBkg) {
        if (_connected) {
            std::fill(_background_spectrum.begin(), _background_spectrum.end(), 0.0);
        }
    } else if (function == P_trigMode) {
        if (_connected) {
            _set_trig_mode(static_cast<uint32_t>(value));
        }
    } else if (function == P_trigDelay) {
        if (_connected) {
            _set_trig_delay(static_cast<uint32_t>(value));
        }
    } else if (function == P_numAve) {
        if (_connected) {
            _set_num_ave(static_cast<uint32_t>(value));
        }
    } else if ((function == P_gpio0Config) || (function == P_gpio1Config) || 
            (function == P_gpio2Config) || (function == P_gpio3Config)) {
        if (_connected) {
            _set_gpio_config(function, value);
        }
    } else if ((function == P_trigInput) || (function == P_trigEdge) || 
            (function == P_trigConf1) || (function == P_trigConf2)) {
        if (_connected) {
            _set_trig_config(function, value);
        }
    } else if (function == P_getSpectrum) {
        if (_connected) {
            _get_spectrum();
        }
    } else {
        /* All other parameters just get set in parameter list, no need to act on them here */
    }
    
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus)callParamCallbacks();
    
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                  "%s::%s: [%s] ERROR: status=%d, function=%d, name=%s, value=%d", 
                  driverName.c_str(), functionName.c_str(), _port_name.c_str(), status, function, paramName, value);
    } else {        
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
              "%s::%s: [%s] function=%d, name=%s, value=%d\n", 
              driverName.c_str(), functionName.c_str(), _port_name.c_str(), function, paramName, value);
    }
    return status;
}


asynStatus drvBroadcom::writeFloat64(asynUser *pasynUser, epicsFloat64 value) {
/*-------------------------------------------------------------------- 
    asynPortDriver override.
----------------------------------------------------------------------*/
    std::string functionName = "writeFloat64";
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    epicsInt32 acquire;
    const char* paramName;

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setDoubleParam(function, value);

    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_updateTime) {
        _update_time = value;
        /* Make sure the update time is valid. If not change it and put back in parameter library */
        if (_update_time < _min_update_time) {
            asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
                    "%s::%s: [%s] Warning: update time too small, changed from %f to %f\n", 
                    driverName.c_str(), functionName.c_str(), _port_name.c_str(), value, _min_update_time);
            _update_time = _min_update_time;
        }
        setDoubleParam(P_updateTime, _update_time);
        /* If the update time has changed and we are acquiring then wake up the acquisition task */
        getIntegerParam(P_acquire, &acquire);
        if (acquire) epicsEventSignal(_event_id);
    } else if (function == P_integrationTime) {
        _set_int_time(value);
    } else {
        /* All other parameters just get set in parameter list, no need to act on them here */
    }
    
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus)callParamCallbacks();
    
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                  "%s::%s: [%s] status=%d, function=%d, name=%s, value=%f", 
                  driverName.c_str(), functionName.c_str(), _port_name.c_str(), status, function, paramName, value);
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
              "%s::%s: [%s] function=%d, name=%s, value=%f\n", 
              driverName.c_str(), functionName.c_str(), _port_name.c_str(), function, paramName, value);
    }
    return status;
}


asynStatus drvBroadcom::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask) {
/*-------------------------------------------------------------------- 
    asynPortDriver override.
----------------------------------------------------------------------*/
    std::string functionName = "writeUInt32Digital";
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char* paramName;

    setUIntDigitalParam(function, value, mask);

    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_procSteps) {
        if (_connected) {
            _set_proc_steps(value);
        }
    }

    status = callParamCallbacks();

    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                "%s::%s: [%s] ERROR: status=%d, function=%d, name=%s, value=0x%x, mask=0x%x\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                status, function, paramName, value, mask);
    } else {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s::%s: [%s] function=%d, name=%s, value=0x%x, mask=0x%x\n",
                driverName.c_str(), functionName.c_str(), _port_name.c_str(),
                function, paramName, value, mask);
    }
    return status;
}


void drvBroadcom::report(FILE *fp, int details) {
/*-------------------------------------------------------------------- 
    asynPortDriver override.
----------------------------------------------------------------------*/
    fprintf(fp, "Broadcom driver (drvBroadcom)\n");
    if (details < 1) {
        fprintf(fp, "\n");
        return;
    }
    
    std::string model;
    getStringParam(P_model, model);
    fprintf(fp, "    Model: %s\n", model.c_str());
    fprintf(fp, "    Serial num: %s\n", _serial_num.c_str());
    fprintf(fp, "    Spectrum length: %zu pixels\n", _spectrum_length);
    fprintf(fp, "\n");

    asynPortDriver::report(fp, details);
}


void drvBroadcom::exitHandler(void *arg) {
/*---------------------------------------------------------
    Exit handler, delete the drvBroadcom object.
-----------------------------------------------------------*/
    drvBroadcom *pdrvBroadcom = (drvBroadcom*)arg;
    if (pdrvBroadcom) delete pdrvBroadcom;
}


// Configuration routine.  Called directly, or from the iocsh function below
extern "C" {
int drvBroadcomConfigure(const char* port, const char* ioPort) {
/*------------------------------------------------------------------------------
 * EPICS iocsh callable function to call constructor for the drvBroadcom class.
 *  port    The name of the asyn port driver to be created.
 *  ioPort  The I/O port created with drvAsynUSBPortConfigure()
 *----------------------------------------------------------------------------*/
    new drvBroadcom(port, ioPort);
    return asynSuccess;
}

static const iocshArg initArg0 = {"port", iocshArgString};
static const iocshArg initArg1 = {"ioPort", iocshArgString};
static const iocshArg* const initArgs[] = {&initArg0, &initArg1};
static const iocshFuncDef initFuncDef = {"drvBroadcomConfigure", 2, initArgs};

static void initCallFunc(const iocshArgBuf *args) {
    drvBroadcomConfigure(args[0].sval, args[1].sval);
}

void drvBroadcomRegister(void){
    iocshRegister(&initFuncDef, initCallFunc);
}

epicsExportRegistrar(drvBroadcomRegister);
}

