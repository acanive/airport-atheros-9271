#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang diagnostic ignored "-Wdocumentation"
#include <libkern/OSByteOrder.h>
#include <libkern/OSKextLib.h>
#include <libkern/version.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#pragma clang diagnostic pop

#include "AirPortAtheros9271.hpp"
#include "Atheros9271Firmware.hpp"

OSDefineMetaClassAndStructors(AirPort_Atheros9271, IOEthernetController);

bool AirPort_Atheros9271::init(OSDictionary *props)
{
    bool result = super::init(props);
    IOLog("Initializing AirPortAtheros9271 driver.\n");
    return result;
}

void AirPort_Atheros9271::free(void)
{
    IOLog("Freeing AirPortAtheros9271 driver resources.\n");
    super::free();
}

IOService *AirPort_Atheros9271::probe(IOService *provider, SInt32 *score)
{
    IOLog("Probing AirPortAtheros9271 driver.\n");
    IOLog("Version %s starting on OS X Darwin %d.%d.\n", OSKextGetCurrentVersionString(), version_major, version_minor);
    
    fCompletionLock = IOLockAlloc();
    if (!fCompletionLock)
        return NULL;
    
    setDevice(provider);
    if (fDevice == NULL)
    {
        IOLog("Provider type is incorrect (not IOUSBHostDevice).\n");
        return NULL;
    }
    
    fProductId = USBToHost16(fDevice->getDeviceDescriptor()->idProduct);
    fVendorId = USBToHost16(fDevice->getDeviceDescriptor()->idVendor);
    fDeviceRev = USBToHost16(fDevice->getDeviceDescriptor()->bcdDevice);
    
    pushDeviceFirmware();

    return super::probe(provider, score);
}

bool AirPort_Atheros9271::attach(IOService *provider)
{
    IOLog("Attaching AirPortAtheros9271 driver.\n");
    return super::attach(provider);
}

void AirPort_Atheros9271::detach(IOService *provider)
{
    IOLog("Detaching AirPortAtheros9271 driver.\n");
    return super::detach(provider);
}

bool AirPort_Atheros9271::start(IOService *provider)
{
    IOLog("Starting AirPortAtheros9271 driver.\n");
    if (!super::start(provider))
        return false;
    
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s", OSKextGetCurrentIdentifier(), OSKextGetCurrentVersionString());
    setProperty("Atheros Driver Version", buf);
    
    
    if (!createMediumTables())
    {
        IOLog("Unable to create medium tables.\n");
        return false;
    }
    
    //    if (!createNetworkInterface())
    //    {
    //        IOLog("Unable to create network interface.\n");
    //        return NULL;
    //    }
    //
    
    setLinkStatus(kIONetworkLinkValid);
    
    
    PMinit();
    provider->joinPMtree(this);
    
    return true;
}

void AirPort_Atheros9271::stop(IOService *provider)
{
    IOLog("Stopping AirPortAtheros9271 driver.\n");
    PMstop();
    
    if (fCompletionLock)
    {
        IOLockFree(fCompletionLock);
        fCompletionLock = NULL;
    }
    
    OSSafeReleaseNULL(fNetworkInterface);
    
    setDevice(NULL);
    super::stop(provider);
}

IOReturn AirPort_Atheros9271::message(UInt32 type, IOService *provider, void *argument)
{
    IOLog("Catching messages in AirPortAtheros9271 driver.\n");
    switch (type) {
        case kUSBHostMessageDeviceConnected:
        case kUSBHostMessageDeviceDisconnected:
        case kUSBHostMessageDeviceSuspend:
        case kUSBHostMessageDeviceResume:
        case kUSBHostMessageConfigurationSet:
        default:
            break;
    }
    
    return super::message(type, provider, argument);
}

bool AirPort_Atheros9271::willTerminate(IOService *provider, IOOptionBits options)
{
//    getOutputQueue()->stop();
//    getOutputQueue()->flush();
//    getOutputQueue()->setCapacity(0);
    
    return super::willTerminate(provider, options);
}

bool AirPort_Atheros9271::terminate(IOOptionBits options)
{
    return super::terminate(options);
}

bool AirPort_Atheros9271::finalize(IOOptionBits options)
{
    return super::finalize(options);
}

IOReturn AirPort_Atheros9271::getHardwareAddress(IOEthernetAddress *addr)
{
    addr->bytes[0] = 0x28;
    addr->bytes[1] = 0xe3;
    addr->bytes[2] = 0x47;
    addr->bytes[3] = 0x15;
    addr->bytes[4] = 0x69;
    addr->bytes[5] = 0x52;
    return kIOReturnSuccess;
}

IOReturn AirPort_Atheros9271::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn result = kIOReturnSuccess;
    
    if (group == gIOEthernetWakeOnLANFilterGroup)
        *filters = 0;
    else if (group == gIONetworkFilterGroup)
    {
        *filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast | kIOPacketFilterPromiscuous | kIOPacketFilterMulticast | kIOPacketFilterMulticastAll;
    }
    else
    {
        result = super::getPacketFilters(group, filters);
    }
    return result;
}

IOReturn AirPort_Atheros9271::selectMedium(const IONetworkMedium *medium)
{
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

IOReturn AirPort_Atheros9271::setMulticastMode(bool active)
{
    return kIOReturnSuccess;
}

IOReturn AirPort_Atheros9271::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
    return kIOReturnSuccess;
}

IOReturn AirPort_Atheros9271::setPromiscuousMode(bool active)
{
    return kIOReturnSuccess;
}

void AirPort_Atheros9271::setDevice(IOService *provider)
{
    OSObject* prevDevice = fDevice;
    fDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (fDevice)
        fDevice->retain();
    
    OSSafeReleaseNULL(prevDevice);
}

void AirPort_Atheros9271::printDeviceInfo()
{
    IOLog("[%04x:%04x]: Atheros 9271 [v%d].\n", fVendorId, fProductId, fDeviceRev);
}

void AirPort_Atheros9271::pushDeviceFirmware()
{
    IOLog("[%04x:%04x]: Pushing device firmware.\n", fVendorId, fProductId);
    if (!fDevice->open(this, 0, 0))
    {
        IOLog("[%04x:%04x]: Could not open the device.\n", fVendorId, fProductId);
        return;
    }
    
    printDeviceInfo();
    
    if (setConfiguration(0) && findInterface() && fInterface->open(this))
    {
        IOLog("[%04x:%04x]: Configuration has been set and interface opened.\n", fVendorId, fProductId);
//        findPipe(fInterruptPipe, kEndpointTypeInterrupt, kEndpointDirectionIn);
//        findPipe(fBulkPipe, kEndpointTypeBulk, kEndpointDirectionOut);
//
//        if (fInterruptPipe != NULL && fBulkPipe != NULL)
//        {
//            IOLog("[%04x:%04x]: Got the pipes.\n", fVendorId, fProductId);
//        }
        
        if (performUpgrade())
        {
            IOLog("[%04x:%04x]: Firmware upgrade completed successfully.\n", fVendorId, fProductId);
        }
        else
            IOLog("[%04x:%04x]: Firmware upgrade failed.\n", fVendorId, fProductId);
        
        fInterface->close(this);
    }
    
    if (fInterruptPipe)
    {
        fInterruptPipe->abort();
        setPipe(fInterruptPipe, NULL);
    }
    
    if (fBulkPipe)
    {
        fBulkPipe->abort();
        setPipe(fBulkPipe, NULL);
    }
    
    setInterface(NULL);
    fDevice->close(this);
}

bool AirPort_Atheros9271::setConfiguration(int configurationIndex)
{
    IOReturn result;
    const ConfigurationDescriptor *configurationDescriptor;
    UInt8 currentConfiguration = 0xFF;
    UInt8 numConf = 0;
    
    IOLog("[%04x:%04x]: Booting up device configurations.\n", fVendorId, fProductId);
    
    if ((numConf = fDevice->getDeviceDescriptor()->bNumConfigurations) < (configurationIndex + 1))
    {
        IOLog("[%04x:%04x]: Composite configuration index %d is not available, %d total composite configurations.\n", fVendorId, fProductId, configurationIndex, numConf);
        
        return false;
    }
    else
    {
        IOLog("[%04x:%04x]: Available composite configurations: %d.\n", fVendorId, fProductId, numConf);
    }
    
    configurationDescriptor = fDevice->getConfigurationDescriptor(configurationIndex);
    if (!configurationDescriptor)
    {
        IOLog("[%04x:%04x]: No configuration descriptor for configuration index: %d.\n", fVendorId, fProductId, configurationIndex);
        return false;
    }
    
    if ((result = getDeviceConfiguration(&currentConfiguration)) != kIOReturnSuccess)
    {
        IOLog("[%04x:%04x]: Unable to retrieve active configuration (0x%08x).\n", fVendorId, fProductId, result);
        return false;
    }
    
    if (currentConfiguration != 0)
    {
        IOLog("[%04x:%04x]: Device configuration is already set to configuration index %d.\n", fVendorId, fProductId, configurationIndex);
        return true;
    }
    
    if ((result = fDevice->setConfiguration(configurationDescriptor->bConfigurationValue)) != kIOReturnSuccess)
    {
        IOLog("[%04x:%04x]: Unable to (re-)configure device (0x%08x).\n", fVendorId, fProductId, result);
        return false;
    }
    
    IOLog("[%04x:%04x]: Set device configuration to configuration index %d successfully.\n", fVendorId, fProductId, configurationIndex);
    
    return true;
}

int AirPort_Atheros9271::getDeviceStatus()
{
    uint16_t stat = 0;
    
    DeviceRequest request;
    request.bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeStandard, kRequestRecipientDevice);
    request.bRequest = kDeviceRequestGetStatus;
    request.wValue = 0;
    request.wIndex = 0;
    request.wLength = sizeof(stat);
    
    uint32_t bytesTransferred = 0;
    IOReturn result = fDevice->deviceRequest(this, request, &stat, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
    
    if (result != kIOReturnSuccess)
    {
        IOLog("[%04x:%04x]: Unable to get device status (0x%08x).\n", fVendorId, fProductId, result);
        return 0;
    }
    
    IOLog("[%04x:%04x]: Device status 0x%08x.\n", fVendorId, fProductId, (int)stat);
    
    return (int)stat;
}

IOReturn AirPort_Atheros9271::pipeCommand(UInt8 requestType, UInt8 command, UInt16 address, IOMemoryDescriptor *buffer)
{
    DeviceRequest request;
    request.bmRequestType = requestType;
    request.bRequest = command;
    request.wValue = address;
    request.wIndex = 0;
    request.wLength = buffer->getLength();
    
    uint32_t bytesTransferred;
    return fInterface->deviceRequest(request, buffer, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
}

IOReturn AirPort_Atheros9271::pipeCommand(UInt8 requestType, UInt8 command, UInt16 address, void *buffer, UInt16 length)
{
    DeviceRequest request;
    request.bmRequestType = requestType;
    request.bRequest = command;
    request.wValue = address;
    request.wIndex = 0;
    request.wLength = length;
    
    uint32_t bytesTransferred;
    return fInterface->deviceRequest(request, buffer, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
}


IOReturn AirPort_Atheros9271::getDeviceConfiguration(UInt8 *configNumber)
{
    uint8_t config = 0;
    
    DeviceRequest request;
    request.bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeStandard, kRequestRecipientDevice);
    request.bRequest = kDeviceRequestGetConfiguration;
    request.wValue = 0;
    request.wIndex = 0;
    request.wLength = sizeof(config);
    
    uint32_t bytesTransferred = 0;
    IOReturn result = fDevice->deviceRequest(this, request, &config, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
    *configNumber = config;
    return result;
}

IOReturn AirPort_Atheros9271::readFromPipe(IOUSBHostPipe *pipe, IOMemoryDescriptor *buffer, IOByteCount length, IOUSBHostCompletion *completion, IOByteCount *bytesRead)
{
    IOReturn result;
    if (completion)
        result = pipe->io(buffer, (UInt32)length, completion);
    else
    {
        uint32_t bytesTransferred;
        result = pipe->io(buffer, (UInt32)length, bytesTransferred);
        if (bytesRead) *bytesRead = bytesTransferred;
    }
    
    return result;
}

IOReturn AirPort_Atheros9271::writeToPipe(IOUSBHostPipe *pipe, IOMemoryDescriptor *buffer, IOByteCount length, IOUSBHostCompletion *completion)
{
    IOReturn result;
    if (completion)
        result = pipe->io(buffer, (UInt32)length, completion);
    else
    {
        uint32_t bytesTransferred;
        result = pipe->io(buffer, (UInt32)length, bytesTransferred);
    }
    
    return result;
}

IOReturn AirPort_Atheros9271::bulkWrite(const void *data, UInt16 length)
{
    IOReturn result;
    if (IOMemoryDescriptor *buffer = IOMemoryDescriptor::withAddress((void*)data, length, kIODirectionIn))
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            if ((result = writeToPipe(fBulkPipe, buffer, buffer->getLength(), NULL)) == kIOReturnSuccess)
            {
                IOLog("[%04x:%04x]: Wrote %d bytes to bulk pipe.\n", fVendorId, fProductId, length);
            }
            else
                IOLog("[%04x:%04x]: Failed to write to bulk pipe (0x%08x).\n", fVendorId, fProductId, result);
        }
        else
            IOLog("[%04x:%04x]: Failed to prepare bulk write memory buffer (0x%08x).\n", fVendorId, fProductId, result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            IOLog("[%04x:%04x]: Failed to complete bulk write memory buffer (0x%08x).\n", fVendorId, fProductId, result);
        
        OSSafeReleaseNULL(buffer);
    }
    else
    {
        IOLog("[%04x:%04x]: Unable to allocate bulk write buffer.\n", fVendorId, fProductId);
        result = kIOReturnNoMemory;
    }
    
    return result;
}

bool AirPort_Atheros9271::resetDevice()
{
    fDevice->setConfiguration(0);
    IOLog("[%04x:%04x]: Device reset.\n", fVendorId, fProductId);
    return true;
}

bool AirPort_Atheros9271::findInterface()
{
    OSIterator *iterator = fDevice->getChildIterator(gIOServicePlane);
    if (!iterator)
        return false;
    
    while (OSObject *candidate = iterator->getNextObject())
    {
        if (IOUSBHostInterface *iface = OSDynamicCast(IOUSBHostInterface, candidate))
        {
            setInterface(iface);
            break;
        }
    }
    
    OSSafeReleaseNULL(iterator);
    
    IOLog("[%04x:%04x]: Interface %d (class %02x, subclass %02x, protocol %02x) located.\n", fVendorId, fProductId,
          fInterface->getInterfaceDescriptor()->bInterfaceNumber,
          fInterface->getInterfaceDescriptor()->bInterfaceClass,
          fInterface->getInterfaceDescriptor()->bInterfaceSubClass,
          fInterface->getInterfaceDescriptor()->bInterfaceProtocol);
    
    return fInterface != NULL;
}

bool AirPort_Atheros9271::findPipe(IOUSBHostPipe *pipe, UInt8 type, UInt8 direction)
{
    const ConfigurationDescriptor *configDesc = fInterface->getConfigurationDescriptor();
    const InterfaceDescriptor *ifaceDesc = fInterface->getInterfaceDescriptor();
    if (!configDesc || !ifaceDesc)
    {
        IOLog("[%04x:%04x]: Cannot find descriptor: configDesc = %p, ifaceDesc = %p.\n", fVendorId, fProductId, configDesc, ifaceDesc);
        return false;
    }
    
    const EndpointDescriptor *epDesc = NULL;
    while ((epDesc = getNextEndpointDescriptor(configDesc, ifaceDesc, epDesc)))
    {
        uint8_t epDirection = getEndpointDirection(epDesc);
        uint8_t epType = getEndpointType(epDesc);
        IOLog("[%04x:%04x]: Endpoint found: direction = %d, type = %d.\n", fVendorId, fProductId, epDirection, epType);
        if (direction == epDirection && type == epType)
        {
            IOLog("[%04x:%04x]: Found matching endpoint.\n", fVendorId, fProductId);
            
            IOUSBHostPipe *p = fInterface->copyPipe(getEndpointAddress(epDesc));
            if (p == NULL)
            {
                IOLog("[%04x:%04x]: Copy pipe failed.\n", fVendorId, fProductId);
                return false;
            }
            
            setPipe(pipe, p);
            OSSafeReleaseNULL(pipe);
            return true;
        }
    }
    
    IOLog("[%04x:%04x]: No matching endpoint found.\n", fVendorId, fProductId);
    return false;
}

bool AirPort_Atheros9271::performUpgrade()
{
    IOLog("[%04x:%04x]: Performing firmware upgrade.\n", fVendorId, fProductId);
    
    OSData *firmwareData = OSData::withBytes(ar9271_fw, ar9271_fw_len);
    bool success = true;
    
    IOLockLock(fCompletionLock);
    
    IOReturn result;
    if (IOMemoryDescriptor *buffer = IOMemoryDescriptor::withAddress((void*)firmwareData->getBytesNoCopy(), firmwareData->getLength(), kIODirectionIn))
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            if ((result = pipeCommand(0x40, FIRMWARE_DOWNLOAD, AR9271_FIRMWARE >> 8, buffer)) != kIOReturnSuccess)
                IOLog("[%04x:%04x]: Unable to write the firmware (0x%08x).\n", fVendorId, fProductId, result);
            else
            {
                if ((result = pipeCommand(0x40, FIRMWARE_DOWNLOAD_COMP, AR9271_FIRMWARE_TEXT >> 8, NULL, 0)) != kIOReturnSuccess)
                    IOLog("[%04x:%04x]: Unable to write the firmware complete sequence (0x%08x).\n", fVendorId, fProductId, result);
                else
                {
                    IOLog("[%04x:%04x]: Success in writing the firmware sequence.\n", fVendorId, fProductId);
                    success = true;
                }
            }
        }
        else
            IOLog("[%04x:%04x]: Failed to prepare write memory buffer (0x%08x).\n", fVendorId, fProductId, result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            IOLog("[%04x:%04x]: Failed to complete write memory buffer (0x%08x).\n", fVendorId, fProductId, result);
        
        OSSafeReleaseNULL(buffer);
    }
    else
        IOLog("[%04x:%04x]: Unable to allocate write memory buffer.\n", fVendorId, fProductId);
    
    IOLockUnlock(fCompletionLock);
    OSSafeReleaseNULL(firmwareData);
    return success;
}

bool AirPort_Atheros9271::createMediumTables()
{
    IOLog("Creating medium tables for AirPortAtheros9271 driver.\n");
    IONetworkMedium *medium;
    
    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (mediumDict == NULL)
    {
        IOLog("Cannot allocate medium dictionary.\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 480 * 1000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    OSSafeReleaseNULL(medium);
    
    bool setResult = false;
    bool result = publishMediumDictionary(mediumDict);
    if (!result)
        IOLog("Cannot publish medium dictionary.\n");
    else
    {
        medium = IONetworkMedium::getMediumWithType(mediumDict, kIOMediumEthernetAuto);
        setResult = setCurrentMedium(medium);
    }
    
    OSSafeReleaseNULL(mediumDict);
    return result && setResult;
}

bool AirPort_Atheros9271::createNetworkInterface()
{
    IOLog("Attaching and registering for AirPortAtheros9271 driver.\n");
 
    if (!attachInterface((IONetworkInterface**)&fNetworkInterface, true))
    {
        IOLog("Attach interface failed.\n");
        return false;
    }
    
    IOLog("Network Interface %p", fNetworkInterface);
    return true;
}


void AirPort_Atheros9271::setInterface(IOService *interface)
{
    OSObject *prev = fInterface;
    fInterface = OSDynamicCast(IOUSBHostInterface, interface);
    
    if (fInterface)
        fInterface->retain();
    
    OSSafeReleaseNULL(prev);
}

void AirPort_Atheros9271::setPipe(IOUSBHostPipe *pipe, OSObject *p)
{
    OSObject *prev = pipe;
    pipe = OSDynamicCast(IOUSBHostPipe, p);

    if (pipe)
        pipe->retain();

    OSSafeReleaseNULL(prev);
}

