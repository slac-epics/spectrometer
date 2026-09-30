#ifndef DRVBROADCOM_H
#define DRVBROADCOM_H

/*==============================================================
#  Abs: C++ header for Broadcom USB spectrometer driver, using the NioLink API.
#
#  Name: drvBroadcom.h
#
#  Desc: This file should be included in drvBroadcom.cpp
#
#  Facility: FACET
#
#  Auth: 1-Jul-2026, M. Dunning (mdunning)
#==============================================================*/

#include "asynPortDriver.h"

#define acquireString             "ACQUIRE"          /* asynInt32               r/w */
#define updateTimeString          "UPDATE_TIME"      /* asynFloat64             r/w */
#define updateRateActString       "UPDATE_RATE_ACT"  /* asynFloat64             r/o */
#define updateTimeActString       "UPDATE_TIME_ACT"  /* asynFloat64             r/o */
#define hwVersionString           "HW_VER"           /* asynInt32               r/o */
#define fwVersionString           "FW_VER"           /* asynInt32               r/o */
#define deviceIDString            "ID"               /* asynInt32               r/o */
#define serialNumString           "SERIAL_NUM"       /* asynOctet               r/o */
#define manufacturerString        "MANU"             /* asynOctet               r/o */
#define modelString               "MODEL"            /* asynOctet               r/o */
#define wavelengthsString         "WAVELENGTHS"      /* asynFloat64Array        r/o */
#define spectrumString            "SPECTRUM"         /* asynFloat64Array        r/o */
#define spectrumLengthString      "SPEC_LEN"         /* asynInt32               r/o */
#define spectrumStatusString      "SPEC_STATUS"      /* asynInt32               r/o */
#define minIntegrationTimeString  "MIN_INT_TIME"     /* asynFloat64             r/o */
#define integrationTimeString     "INT_TIME"         /* asynFloat64             r/w */
#define shutterString             "SHUTTER"          /* asynInt32               r/w */
#define numAveString              "NUM_AVE"          /* asynInt32               r/w */
#define smoothingWidthString      "SMO_WID"          /* asynInt32               r/w */
#define subtractBkgString         "BKG"              /* asynInt32               r/w */
#define getBkgString              "GET_BKG"          /* asynInt32               r/w */
#define clearBkgString            "CLEAR_BKG"        /* asynInt32               r/w */
#define connStatusString          "CONN"             /* asynInt32               r/o */
#define reconnectString           "RECONN"           /* asynInt32               r/w */
#define resetString               "RESET"            /* asynInt32               r/w */
#define trigModeString            "TRIG_MODE"        /* asynInt32               r/w */
#define trigDelayString           "TRIG_DELAY"       /* asynInt32               r/w */
#define trigInputString           "TRIG_INP"         /* asynInt32               r/w */
#define trigEdgeString            "TRIG_EDGE"        /* asynInt32               r/w */
#define trigConf1String           "TRIG_CONF1"       /* asynInt32               r/w */
#define trigConf2String           "TRIG_CONF2"       /* asynInt32               r/w */
#define tempString                "TEMP"             /* asynFloat64             r/w */
#define procStepsString           "PROC_STEPS"       /* asynParamUInt32Digital  r/w */
#define gpio0ConfigString         "GPIO0_CONF"       /* asynInt32               r/w */
#define gpio1ConfigString         "GPIO1_CONF"       /* asynInt32               r/w */
#define gpio2ConfigString         "GPIO2_CONF"       /* asynInt32               r/w */
#define gpio3ConfigString         "GPIO3_CONF"       /* asynInt32               r/w */
#define eventCodeString           "EVENT_CODE"       /* asynInt32               r/w */
#define getSpectrumString         "GET_SPECT"        /* asynInt32               r/w */


class drvBroadcom : public asynPortDriver {
public:
    drvBroadcom(const char* port, const char* ioPort);
    virtual ~drvBroadcom();
    virtual void pollerThread();
    static void exitHandler(void*);

    /* These are the methods that we override from asynPortDriver */
    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask);
    virtual void report(FILE *fp, int details);

protected:
    int P_acquire;
    int P_updateTime;
    int P_updateTimeAct;
    int P_updateRateAct;
    int P_hwVersion;
    int P_fwVersion;
    int P_deviceID;
    int P_serialNum;
    int P_manufacturer;
    int P_model;
    int P_wavelengths;
    int P_spectrum;
    int P_spectrumLength;
    int P_spectrumStatus;
    int P_minIntegrationTime;
    int P_integrationTime;
    int P_shutter;
    int P_numAve;
    int P_smoothingWidth;
    int P_subtractBkg;
    int P_getBkg;
    int P_clearBkg;
    int P_conn;
    int P_reconn;
    int P_reset;
    int P_trigMode;
    int P_trigDelay;
    int P_trigInput;
    int P_trigEdge;
    int P_trigConf1;
    int P_trigConf2;
    int P_temp;
    int P_procSteps;
    int P_gpio0Config;
    int P_gpio1Config;
    int P_gpio2Config;
    int P_gpio3Config;
    int P_eventCode;
    int P_getSpectrum;

private:
    asynUser *pasynUser;
    bool _running;
    bool _exited;
    int _error_count;
    epicsEventId _event_id;
    std::vector<epicsFloat32> _wavelengths;
    std::vector<epicsFloat32> _spectrum;
    std::vector<epicsFloat32> _background_spectrum;
    size_t _spectrum_length;
    double _update_time;
    double _min_update_time;
    std::string _port_name;
    std::string _io_port_name;
    std::string _serial_num;
    bool _connected;
    bool _connect();
    bool _disconnect();
    void _reset();
    bool _get_device_features();
    void _get_spectrum(bool is_background=false);
    uint16_t _unpack_spectrum_header(const std::vector<uint8_t> header);
    void _get_bg_spectrum();
    void _get_int_time();
    void _set_int_time(epicsFloat64 value);
    void _get_trig_delay();
    void _set_trig_delay(uint32_t value);
    void _get_trig_mode();
    void _set_trig_mode(uint32_t value);
    void _get_trig_config();
    void _set_trig_config(int function, epicsInt32 value);
    void _get_gpio_config();
    void _set_gpio_config(int function, epicsInt32 value);
    void _get_num_ave();
    void _set_num_ave(uint32_t value);
    void _get_temp();
    void _get_proc_steps();
    void _set_proc_steps(uint32_t value);
    size_t _writeReadDevice(const uint8_t* cmd_str, size_t cmd_str_size, uint8_t* resp, size_t resp_size);
    std::string _getString(const uint8_t* cmd_str, size_t cmd_str_size);
    uint32_t _getUInt(const uint8_t* cmd_str, size_t cmd_str_size);
    void _setUInt(const uint8_t* cmd_str, size_t cmd_str_size, uint32_t value);
    int32_t _getInt(const uint8_t* cmd_str, size_t cmd_str_size);
    double _getFloat(const uint8_t* cmd_str, size_t cmd_str_size);
    uint32_t _bytes_to_uint(const uint8_t* byte_array);
    void _uint_to_bytes(uint32_t value, uint8_t* byte_array);
    int32_t _bytes_to_int(const uint8_t* byte_array);
    double _bytes_to_float(const uint8_t* byte_array);
    uint16_t _bytes_to_ushort(const uint8_t* byte_array);
};

#endif

