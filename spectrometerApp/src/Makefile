TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#========================================

USR_CXXFLAGS += -std=c++11

# Use new typed rset defs (epics v7)
USR_CXXFLAGS += -DUSE_TYPED_RSET

# Set a flag if we're using an EVR, so that the driver gets EVR timestamps
ifdef EVRCLIENT
USR_CXXFLAGS += -DEVR_SUPPORT
endif

#========================================
# Build an IOC support library
PROD_NAME = spectrometer

ifeq ($(T_A), linuxRT-x86_64)
LIBRARY_IOC_Linux += $(PROD_NAME)RT
PROD_IOC_Linux += $(PROD_NAME)RT
DBD += $(PROD_NAME)RT.dbd
USR_INCLUDES += -I$(OCEANDIRECT_INCLUDE)
else ifeq ($(T_A), rhel7-x86_64)
LIBRARY_IOC_Linux += $(PROD_NAME)
PROD_IOC_Linux += $(PROD_NAME)
DBD += $(PROD_NAME)_RHEL7.dbd
USR_INCLUDES += -I$(SEABREEZE_INCLUDE)
else ifeq ($(T_A), rhel9-x86_64)
LIBRARY_IOC_Linux += $(PROD_NAME)
PROD_IOC_Linux += $(PROD_NAME)
DBD += $(PROD_NAME)_RHEL9.dbd
USR_INCLUDES += -I$(SEABREEZE_INCLUDE)
USR_INCLUDES += -I$(OCEANDIRECT_INCLUDE)
endif

ifeq ($(T_A), linuxRT-x86_64)
$(PROD_NAME)RT_DBD += base.dbd
$(PROD_NAME)RT_DBD += asyn.dbd
$(PROD_NAME)RT_DBD += drvAsynUSBPort.dbd
$(PROD_NAME)RT_DBD += oceanDirectSupport.dbd
$(PROD_NAME)RT_DBD += broadcomSupport.dbd
else ifeq ($(T_A), rhel7-x86_64)
$(PROD_NAME)_RHEL7_DBD += base.dbd
$(PROD_NAME)_RHEL7_DBD += asyn.dbd
$(PROD_NAME)_RHEL7_DBD += drvAsynUSBPort.dbd
$(PROD_NAME)_RHEL7_DBD += seabreezeSupport.dbd
$(PROD_NAME)_RHEL7_DBD += broadcomSupport.dbd
else ifeq ($(T_A), rhel9-x86_64)
$(PROD_NAME)_RHEL9_DBD += base.dbd
$(PROD_NAME)_RHEL9_DBD += asyn.dbd
$(PROD_NAME)_RHEL9_DBD += drvAsynUSBPort.dbd
$(PROD_NAME)_RHEL9_DBD += seabreezeSupport.dbd
$(PROD_NAME)_RHEL9_DBD += oceanDirectSupport.dbd
$(PROD_NAME)_RHEL9_DBD += broadcomSupport.dbd
endif

# The following are compiled and added to the Support library

ifeq ($(T_A), linuxRT-x86_64)
LIB_INSTALLS_Linux += $(OCEANDIRECT_LIB)/liboceandirect.so
LIB_INSTALLS_Linux += $(wildcard $(LIBUSB01_LIB)/libusb-0.1.so*)
else ifeq ($(T_A), rhel7-x86_64)
LIB_INSTALLS_Linux += $(SEABREEZE_LIB)/libseabreeze.a
else ifeq ($(T_A), rhel9-x86_64)
LIB_INSTALLS_Linux += $(OCEANDIRECT_LIB)/liboceandirect.so
LIB_INSTALLS_Linux += $(SEABREEZE_LIB)/libseabreeze.a
endif

ifeq ($(T_A), linuxRT-x86_64)
$(PROD_NAME)RT_SRCS += drvOceanDirect.cpp
$(PROD_NAME)RT_SRCS += drvAsynUSBPort.c
$(PROD_NAME)RT_SRCS += drvBroadcom.cpp
$(PROD_NAME)RT_SRCS += spectrometerMain.cpp
else ifeq ($(T_A), rhel7-x86_64)
$(PROD_NAME)_SRCS += drvSeaBreezeWrapper.cpp
$(PROD_NAME)_SRCS += drvSeaBreezeAPI.cpp
$(PROD_NAME)_SRCS += drvAsynUSBPort.c
$(PROD_NAME)_SRCS += drvBroadcom.cpp
$(PROD_NAME)_SRCS += spectrometerMain.cpp
else ifeq ($(T_A), rhel9-x86_64)
$(PROD_NAME)_SRCS += drvSeaBreezeWrapper.cpp
$(PROD_NAME)_SRCS += drvSeaBreezeAPI.cpp
$(PROD_NAME)_SRCS += drvOceanDirect.cpp
$(PROD_NAME)_SRCS += drvAsynUSBPort.c
$(PROD_NAME)_SRCS += drvBroadcom.cpp
$(PROD_NAME)_SRCS += spectrometerMain.cpp
endif


##### LinuxRT
ifeq ($(T_A), linuxRT-x86_64)
$(PROD_NAME)RT_LIBS += asyn
$(PROD_NAME)RT_SYS_LIBS += oceandirect

ifdef EVRCLIENT
$(PROD_NAME)RT_LIBS += evrSupport
endif

$(PROD_NAME)RT_SYS_LIBS_Linux += usb-0.1 usb-1.0
$(PROD_NAME)RT_LIBS += $(EPICS_BASE_IOC_LIBS)
##### RHEL7
else ifeq ($(T_A), rhel7-x86_64)
$(PROD_NAME)_LIBS += asyn
$(PROD_NAME)_LIBS += seabreeze

ifdef EVRCLIENT
$(PROD_NAME)_LIBS += evrSupport
endif

$(PROD_NAME)_SYS_LIBS += usb usb-1.0
$(PROD_NAME)_LIBS += $(EPICS_BASE_IOC_LIBS)
##### RHEL9
else ifeq ($(T_A), rhel9-x86_64)
$(PROD_NAME)_LIBS += asyn
$(PROD_NAME)_LIBS += seabreeze

ifdef EVRCLIENT
$(PROD_NAME)_LIBS += evrSupport
endif

$(PROD_NAME)_LIBS += seabreeze
$(PROD_NAME)_SYS_LIBS += usb usb-1.0
$(PROD_NAME)_SYS_LIBS += oceandirect
$(PROD_NAME)_LIBS += $(EPICS_BASE_IOC_LIBS)
endif

#========================================
include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# End of file

