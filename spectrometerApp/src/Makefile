TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#========================================

# =====================================================
# Path to "NON EPICS" External PACKAGES: USER INCLUDES
# ======================================================
USR_INCLUDES += -I$(SEABREEZE_INCLUDE)

#========================================
# Build an IOC support library
PROD_NAME = spectrometer
LIBRARY_IOC += $(PROD_NAME)

# Install dbd files
DBD += $(PROD_NAME).dbd

$(PROD_NAME)_DBD += base.dbd
$(PROD_NAME)_DBD += asyn.dbd
$(PROD_NAME)_DBD += $(PROD_NAME)Support.dbd

# The following are compiled and added to the Support library
$(PROD_NAME)_SRCS += drvSeaBreezeWrapper.cpp
$(PROD_NAME)_SRCS += drvSeaBreezeAPI.cpp

$(PROD_NAME)_LIBS += asyn
$(PROD_NAME)_LIBS += $(EPICS_BASE_IOC_LIBS)

#========================================
include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
#
# End of file

