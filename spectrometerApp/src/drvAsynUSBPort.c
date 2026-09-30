// From Tyler Johnson, PCDS

#include<epicsExit.h>
#include<epicsString.h>
#include<epicsExport.h>
#include<asynDriver.h>
#include<asynOctet.h>
#include<asynOption.h>
#include<libusb-1.0/libusb.h>
#include<string.h>
#include<stdio.h>
#include<assert.h> 
#include<stdlib.h>
#include<iocsh.h>
#include<cantProceed.h>

typedef struct usbAsynPort {
    asynUser      *pasynUser;
    char          *portName;
    libusb_device_handle *dev;
    int            vendorID;
    int            prodID;
    char          *serial;
    int            readPort;
    int            writePort;
    int            flags;
#define FLAG_CONNECT_PER_TRANSACTION    0x1
#define FLAG_SHUTDOWN                   0x2
    unsigned long  nRead;
    unsigned long  nWritten;
    asynInterface  common;
    asynInterface  option;
    asynInterface  octet;
    asynOctet      octet_if;
#define MAXRXDATA    16384
    unsigned char  readBuf[MAXRXDATA];
    int            readSize;
    int            readCur;
} usbAsynPort_t;

static libusb_context *ctx = NULL;
static int usb_cnt = 0;

static asynStatus
connectIt(void *drvPvt, asynUser *pasynUser)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;
    libusb_device **devs;
    int i, r, cnt;

    assert(pasyn);
    assert(!pasyn->dev);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "Opening connection %s\n", pasyn->portName);
    
    cnt = libusb_get_device_list(ctx, &devs); 
    if (cnt < 0) {
        printf("USB connect: get device error.\n");
        return asynError;
    }
    for(i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        r = libusb_get_device_descriptor(devs[i], &desc);
        if (r < 0) {
            printf("Failed to get device descriptor?\n");
            continue;
        }
        if (desc.idVendor == pasyn->vendorID && desc.idProduct == pasyn->prodID) {
            if (libusb_open(devs[i], &pasyn->dev)) {
                printf("Open failed!\n");
                pasyn->dev = NULL;
                continue;
            }
            usb_cnt++;
            if (!pasyn->serial) {
                asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Connection to %s established!\n", pasyn->portName);
                break;  /* No serial number requested --> this one is fine. */
            } else {
                unsigned char buf[128];
                //const char buf[128];
                buf[0] = 0;
                r = libusb_get_string_descriptor_ascii(pasyn->dev, desc.iSerialNumber, buf, sizeof(buf));
                if (strcmp((char *)buf, pasyn->serial)) {
                    libusb_close(pasyn->dev); /* Not the one! */
                    usb_cnt--;
                    pasyn->dev = NULL;
                } else {
                    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                              "Connection to %s established!\n", pasyn->portName);
                    break; /* Got it! */
                }
            }
        }
    }
    if (pasyn->dev) {
        if (libusb_kernel_driver_active(pasyn->dev, 0) == 1) { 
            asynPrint(pasynUser, ASYN_TRACE_FLOW,
                      "Kernel driver active for %s!\n", pasyn->portName);
            if (libusb_detach_kernel_driver(pasyn->dev, 0) == 0) 
                asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Kernel driver detached from %s.\n", pasyn->portName);
        }
        r = libusb_claim_interface(pasyn->dev, 0); 
        if (r < 0) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "Cannot claim interface for %s.\n", pasyn->portName);
            libusb_close(pasyn->dev);
            usb_cnt--;
            pasyn->dev = NULL;
        } else {
            asynPrint(pasynUser, ASYN_TRACE_FLOW,
                      "Claimed interface for %s.\n", pasyn->portName);
        }
        libusb_reset_device(pasyn->dev);
    }
    libusb_free_device_list(devs, 1); 
    return pasyn->dev ? asynSuccess : asynError;
}

static void
closeIt(asynUser *pasynUser, usbAsynPort_t *pasyn, const char *why)
{
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "Close %s connection: %s\n", pasyn->portName, why);
    if (pasyn->dev) {
        libusb_release_interface(pasyn->dev, 0);
        libusb_close(pasyn->dev);
        usb_cnt--;
        pasyn->dev = NULL;
    }
    if (!(pasyn->flags & FLAG_CONNECT_PER_TRANSACTION) ||
         (pasyn->flags & FLAG_SHUTDOWN))
        pasynManager->exceptionDisconnect(pasynUser);
}

/*
 * asynCommon methods
 */
static void
asynCommonReport(void *drvPvt, FILE *fp, int details)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;

    assert(pasyn);
    if (details >= 1) {
        // ...
    }
}

static asynStatus
asynCommonConnect(void *drvPvt, asynUser *pasynUser)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;
    asynStatus status = asynSuccess;

    if (!(pasyn->flags & FLAG_CONNECT_PER_TRANSACTION))
        status = connectIt(drvPvt, pasynUser);
    if (status == asynSuccess)
        pasynManager->exceptionConnect(pasynUser);
    return status;
}

static asynStatus
asynCommonDisconnect(void *drvPvt, asynUser *pasynUser)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;

    assert(pasyn);
    closeIt(pasynUser, pasyn, "Disconnect request");
    if (pasyn->flags & FLAG_CONNECT_PER_TRANSACTION)
        pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

static const struct asynCommon asynCommonMethods = {
    asynCommonReport,
    asynCommonConnect,
    asynCommonDisconnect
};

static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data, size_t numchars,size_t *nbytesTransfered)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;
    asynStatus status = asynSuccess;
    int writePollmsec, actual, r;

    assert(pasyn);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s write.", pasyn->portName);
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                "%s write %lu", pasyn->portName, (unsigned long)numchars);
    *nbytesTransfered = 0;
    if (!pasyn->dev) {
        if (pasyn->flags & FLAG_CONNECT_PER_TRANSACTION) {
            if ((status = connectIt(drvPvt, pasynUser)) != asynSuccess)
                return status;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                          "%s disconnected:", pasyn->portName);
            return asynError;
        }
    }
    if (numchars == 0)
        return asynSuccess;
    writePollmsec = (int) (pasynUser->timeout * 1000.0);
    if (writePollmsec == 0) writePollmsec = 1;
    if (writePollmsec < 0) writePollmsec = 0;
    for (;;) {
        actual = 0;
        r = libusb_bulk_transfer(pasyn->dev, (pasyn->writePort | LIBUSB_ENDPOINT_OUT),
                                 (unsigned char *)data, numchars, &actual, writePollmsec);
        if (r) {
            if (r == LIBUSB_ERROR_TIMEOUT)
                status = asynTimeout;
            else {
                closeIt(pasynUser, pasyn, "Write error");
                status = asynError;
            }
        }
        pasyn->nWritten += actual;
        *nbytesTransfered += actual;
        numchars -= actual;
        data += actual;
        if (numchars == 0 || r != 0)  /* Done or timed out, stop! */
            break;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "wrote %lu to %s, return %s.\n", (unsigned long)*nbytesTransfered,
                                               pasyn->portName,
                                               pasynManager->strStatus(status));
    return status;
}

static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data, size_t maxchars, size_t *nbytesTransfered, int *gotEom)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;
    int readPollmsec;
    int r;

    asynStatus status = asynSuccess; // TJ

    assert(pasyn);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s read (max=%lu, timeout=%lg).\n", pasyn->portName, maxchars, pasynUser->timeout);
    if (!pasyn->dev) {
        if (pasyn->flags & FLAG_CONNECT_PER_TRANSACTION) {
            if ((status = connectIt(drvPvt, pasynUser)) != asynSuccess)
                return status;
        } else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "%s disconnected:", pasyn->portName);
            return asynError;
        }
    }
    if (maxchars <= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                  "%s maxchars %d. Why <=0?", pasyn->portName, (int)maxchars);
        return asynError;
    }

    if (pasyn->readCur >= pasyn->readSize) { /* Try to read some data! */
        readPollmsec = (int) (pasynUser->timeout * 1000.0);
        if (readPollmsec == 0) readPollmsec = 1;
        if (readPollmsec < 0) readPollmsec = 0;

        pasyn->readCur = 0;
        pasyn->readSize = 0;
        r = libusb_bulk_transfer(pasyn->dev, (pasyn->readPort | LIBUSB_ENDPOINT_IN), 
                                 pasyn->readBuf, MAXRXDATA, &pasyn->readSize, readPollmsec);
        if (pasyn->readSize >= 0) {
            if (pasynTrace->getTraceMask(pasynUser) & ASYN_TRACEIO_DRIVER) {
                asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, (const char *)pasyn->readBuf, 
			    pasyn->readSize, "%s real read %d", pasyn->portName,
			    pasyn->readSize);
            }
            pasyn->nRead += pasyn->readSize;
        }
        if (r) {
            if (r == LIBUSB_ERROR_TIMEOUT) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s timeout", pasyn->portName);
                status = asynTimeout;
            } else {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s read error: %d", pasyn->portName, r);
                closeIt(pasynUser, pasyn, "Read error");
                status = asynError;
            }
        }
    }

    if (pasyn->readSize > pasyn->readCur) { /* Just give back queued bytes! */
        int size = pasyn->readSize - pasyn->readCur;
        if (size > maxchars)
            size = maxchars;
        memcpy(data, &pasyn->readBuf[pasyn->readCur], size);
        pasyn->readCur += size;
        *nbytesTransfered = size;
        if (pasynTrace->getTraceMask(pasynUser) & ASYN_TRACEIO_DRIVER) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, size,
                        "%s read %d", pasyn->portName, size);
        }
        return asynSuccess;
    }

    *nbytesTransfered = pasyn->readSize;
    if (gotEom) *gotEom = 0;
    return status;
}

static asynStatus flushIt(void *drvPvt,asynUser *pasynUser)
{
    return asynSuccess;
}

/*
 * asynOption methods
 */
static asynStatus
getOption(void *drvPvt, asynUser *pasynUser, const char *key, char *val, int valSize)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;

    assert(pasyn);
    /*
     * Look at key, fetch its value and put it into val.  Return asynError/asynSuccess.
     */
    return asynError;
}

static asynStatus
setOption(void *drvPvt, asynUser *pasynUser, const char *key, const char *val)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;

    assert(pasyn);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s setOption key %s val %s\n", pasyn->portName, key, val);
    /*
     * Look at key, and put the value where it belongs.  Return asynError/asynSuccess.
     */
    return asynError;
}
static const struct asynOption asynOptionMethods = { setOption, getOption };

static void
final_cleanup (void *arg)
{
    asynStatus status;
    usbAsynPort_t *pasyn = (usbAsynPort_t *)arg;

    if (!pasyn)
        return;
    status=pasynManager->lockPort(pasyn->pasynUser);
    if (status!=asynSuccess)
        asynPrint(pasyn->pasynUser, ASYN_TRACE_ERROR, "%s: cleanup locking error\n", pasyn->portName);
    if (pasyn->dev) {
        pasyn->flags |= FLAG_SHUTDOWN;
        libusb_close(pasyn->dev);
        usb_cnt--;
        pasyn->dev = NULL;
    }
    if (!usb_cnt)
        libusb_exit(ctx); 
    if(status==asynSuccess)
        pasynManager->unlockPort(pasyn->pasynUser);
}

static void
cleanup(usbAsynPort_t *pasyn)
{
    if (pasyn) {
        free(pasyn->portName);
        if (pasyn->dev) {
            libusb_close(pasyn->dev);
            pasyn->dev = NULL;
        }
        free(pasyn);
    }
}

static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
                              const char *eos,int eoslen)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;
    static int first = 1;
    if (first) {
        printf("WARNING: setInputEos is ignored for port %s\n", pasyn->portName);
        first = 0;
    }
    return asynSuccess;
}

static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
                              char *eos, int eossize, int *eoslen)
{
    *eos = 0;
    *eoslen = 0;
    return asynSuccess;
}

static asynStatus setOutputEos(void *drvPvt,asynUser *pasynUser,
                               const char *eos,int eoslen)
{
    usbAsynPort_t *pasyn = (usbAsynPort_t *)drvPvt;
    static int first = 1;
    if (first) {
        printf("WARNING: setOutputEos is ignored for port %s\n", pasyn->portName);
        first = 0;
    }
    return asynSuccess;
}

static asynStatus getOutputEos(void *drvPvt,asynUser *pasynUser,
                               char *eos, int eossize, int *eoslen)
{
    *eos = 0;
    *eoslen = 0;
    return asynSuccess;
}

epicsShareFunc int drvAsynUSBPortConfigure(const char *portName, int vendorID, int prodID,
                                           char *serial, int readPort, int writePort)
{
    usbAsynPort_t *pasyn;
    static int firsttime = 1;

    if (firsttime) {
        int r = libusb_init(&ctx);
        if (r < 0) {
            printf("Cannot initialize libusb: error %d\n", r);
            return -1;
        }
        //libusb_set_debug(ctx, 3); 
        firsttime = 0;
    }

    asynStatus status; // TJ

    if (portName == NULL) {
        printf("Port name missing.\n");
        return -1;
    }
    pasyn = (usbAsynPort_t *)callocMustSucceed(1, sizeof(usbAsynPort_t), "drvAsynUSBPortConfigure()");
    pasyn->portName = epicsStrDup(portName);
    pasyn->dev = NULL;
    pasyn->vendorID = vendorID;
    pasyn->prodID = prodID;
    if (serial)
        pasyn->serial = epicsStrDup(serial);
    else
        pasyn->serial = NULL;
    pasyn->readPort = readPort;
    pasyn->writePort = writePort;
    pasyn->flags = 0;
    pasyn->nRead = 0;
    pasyn->nWritten = 0;
    pasyn->readSize = 0;
    pasyn->readCur = 0;

    pasyn->common.interfaceType = asynCommonType;
    pasyn->common.pinterface  = (void *)&asynCommonMethods;
    pasyn->common.drvPvt = pasyn;
    pasyn->option.interfaceType = asynOptionType;
    pasyn->option.pinterface  = (void *)&asynOptionMethods;
    pasyn->option.drvPvt = pasyn;

    if (pasynManager->registerPort(pasyn->portName,
                                   ASYN_CANBLOCK,
                                   1, /* auto connect */
                                   0, /* medium priority */
                                   0) /* default stack size */ != asynSuccess) {
        printf("drvAsynUSBPortConfigure: Can't register myself.\n");
        cleanup(pasyn);
        return -1;
    }
    status = pasynManager->registerInterface(pasyn->portName,&pasyn->common);
    if(status != asynSuccess) {
        printf("drvAsynUSBPortConfigure: Can't register common.\n");
        cleanup(pasyn);
        return -1;
    }
    status = pasynManager->registerInterface(pasyn->portName,&pasyn->option);
    if(status != asynSuccess) {
        printf("drvAsynUSBPortConfigure: Can't register option.\n");
        cleanup(pasyn);
        return -1;
    }

    pasyn->octet.interfaceType = asynOctetType;
    pasyn->octet.pinterface  = (void *)&pasyn->octet_if;
    pasyn->octet.drvPvt = pasyn;
    pasyn->octet_if.read = readIt;
    pasyn->octet_if.write = writeIt;
    pasyn->octet_if.flush = flushIt;
    pasyn->octet_if.setInputEos = setInputEos;
    pasyn->octet_if.setOutputEos = setOutputEos;
    pasyn->octet_if.getInputEos = getInputEos;
    pasyn->octet_if.getOutputEos = getOutputEos;
    status = pasynOctetBase->initialize(pasyn->portName,&pasyn->octet, 0, 0, 1);
    if(status != asynSuccess) {
        printf("drvAsynUSBPortConfigure: pasynOctetBase->initialize failed.\n");
        cleanup(pasyn);
        return -1;
    }

    pasyn->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasyn->pasynUser,pasyn->portName,-1);
    if(status != asynSuccess) {
        printf("connectDevice failed %s\n",pasyn->pasynUser->errorMessage);
        cleanup(pasyn);
        return -1;
    }

    /*
     * Register for socket cleanup
     */
    epicsAtExit(final_cleanup, pasyn);
    return 0;
}

/*
 * IOC shell command registration
 */
static const iocshArg drvAsynUSBPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynUSBPortConfigureArg1 = { "vendorID",iocshArgInt};
static const iocshArg drvAsynUSBPortConfigureArg2 = { "prodID",iocshArgInt};
static const iocshArg drvAsynUSBPortConfigureArg3 = { "serial",iocshArgString};
static const iocshArg drvAsynUSBPortConfigureArg4 = { "read endpoint",iocshArgInt};
static const iocshArg drvAsynUSBPortConfigureArg5 = { "write endpoint",iocshArgInt};
static const iocshArg *drvAsynUSBPortConfigureArgs[] = {
    &drvAsynUSBPortConfigureArg0, &drvAsynUSBPortConfigureArg1,
    &drvAsynUSBPortConfigureArg2, &drvAsynUSBPortConfigureArg3,
    &drvAsynUSBPortConfigureArg4, &drvAsynUSBPortConfigureArg5};
static const iocshFuncDef drvAsynUSBPortConfigureFuncDef =
                      {"drvAsynUSBPortConfigure",6,drvAsynUSBPortConfigureArgs};
static void drvAsynUSBPortConfigureCallFunc(const iocshArgBuf *args)
{
    drvAsynUSBPortConfigure(args[0].sval, args[1].ival, args[2].ival,
                            args[3].sval, args[4].ival, args[5].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvAsynUSBPortRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvAsynUSBPortConfigureFuncDef,drvAsynUSBPortConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvAsynUSBPortRegisterCommands);
