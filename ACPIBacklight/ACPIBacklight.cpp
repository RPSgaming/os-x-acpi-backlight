/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include "ACPIBacklight.h"
#include "Debug.h"

#define super IODisplayParameterHandler

OSDefineMetaClassAndStructors(ACPIBacklightPanel, IODisplayParameterHandler)


#pragma mark -
#pragma mark IOService functions override
#pragma mark -


bool ACPIBacklightPanel::init()
{
	DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    backLightDevice = NULL;
    BCLlevels = NULL;
    gpuDevice = NULL;
    _display = NULL;
	
    IOACPIPlane = IORegistryEntry::getPlane("IOACPIPlane");
    
	return super::init();
}


IOService * ACPIBacklightPanel::probe( IOService * provider, SInt32 * score )
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    IOACPIPlane = IORegistryEntry::getPlane("IOACPIPlane");
    
    bool hasFound = findDevices(provider);
    DbgLog("%s: probe(devices found : %s)\n", this->getName(), (hasFound ? "true" : "false") );
    
    if (!hasFound)
        return NULL;
    
    DbgLog("%s: %s has backlight Methods\n", this->getName(), backLightDevice->getName());
    
    return super::probe(provider, score);
}


bool ACPIBacklightPanel::start( IOService * provider )
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    findDevices(provider);
    getDeviceControl();
    hasSaveMethod = hasSAVEMethod(backLightDevice);
    
    min = 0;
    max = setupIndexedLevels();
    value = getIndexForLevel(queryACPICurentBrightnessLevel());
    //initialise depending on the AC or Bat status
    if (getACStatus())
        value = max(minAC, value);
    else
        value = min(maxBat, value);
    
    DbgLog("%s: min = %u, max = %u, value = %u\n", this->getName(), (unsigned int)min, (unsigned int)max, (unsigned int)value);
	
	IOLog("%s: Version 1.2\n", this->getName());
	return true;
}


void ACPIBacklightPanel::stop( IOService * provider )
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
	
    return (super::stop(provider));
}


void ACPIBacklightPanel::free()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
	
	if (gpuDevice)
		gpuDevice->release();
    
	if (backLightDevice)
        backLightDevice->release();
    
	if (BCLlevels)
		IODelete(BCLlevels,SInt32,BCLlevelsCount);
	
    if (_display)
        _display->release();
    
    super::free();
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma mark -
#pragma mark IODisplayParameterHandler functions override
#pragma mark -


bool ACPIBacklightPanel::setDisplay( IODisplay * display )
{    
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    if (_display)
    {
        DbgLog("%s::%s releasing display\n", this->getName(),__FUNCTION__);
        _display->release();
    }
    _display = display;
    _display->retain();
    return true;
}


bool ACPIBacklightPanel::doIntegerSet( OSDictionary * params, const OSSymbol * paramName, UInt32 osxValue )
{
    if ( gIODisplayBrightnessKey->isEqualTo(paramName) )
    {   
        UInt32 drvValue = osxValue * (max-min) / 0x400 + min;
        DbgLog("%s::%s(%s) map %d -> %d\n", this->getName(),__FUNCTION__, paramName->getCStringNoCopy(), (unsigned int)osxValue, (unsigned int)drvValue);
        setACPIBrightnessLevel( BCLlevels[drvValue % BCLlevelsCount]);
        this->value = drvValue;
        return true;
    }
    else if (gIODisplayParametersCommitKey->isEqualTo(paramName) )
    {
        UInt32 osxValue = ((this->value-min) * 0x400 / (max-min));
        DbgLog("%s::%s(%s) map %d -> %d\n", this->getName(),__FUNCTION__, paramName->getCStringNoCopy(), (unsigned int)osxValue, (unsigned int)this->value);
        IODisplay::setParameter(params, gIODisplayBrightnessKey, osxValue);
        if (hasSaveMethod)
            saveACPIBrightnessLevel(BCLlevels[(UInt32)this->value]);
        return true;
    }
    return false;
}


bool ACPIBacklightPanel::doDataSet( const OSSymbol * paramName, OSData * value )
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    return true;
}


bool ACPIBacklightPanel::doUpdate( void )
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    OSDictionary * backlightParams, * allParams, * myParams;
    OSDictionary *   newDict = 0;
    
	allParams = OSDynamicCast(OSDictionary, _display->copyProperty(gIODisplayParametersKey));
    if (allParams)
    {
        newDict = OSDictionary::withDictionary(allParams);
        allParams->release();
    }
    
    backlightParams = OSDictionary::withCapacity(2);
    
    myParams  = OSDynamicCast(OSDictionary, copyProperty(gIODisplayParametersKey));
    if (backlightParams && myParams)
	{				
		DbgLog("%s: ACPILevel min %d, max %d, value %d\n", this->getName(), (int)min, (int)max, (int)value);
		
        IODisplay::addParameter(backlightParams, gIODisplayBrightnessKey, 0x000, 0x400);
        IODisplay::setParameter(backlightParams, gIODisplayBrightnessKey, ((value-min) * 0x400 / (max-min)));
        
        OSNumber * num = OSNumber::withNumber(0ULL, 32);
        OSDictionary * commitParams = OSDictionary::withCapacity(1);
        commitParams->setObject("reg", num);
        backlightParams->setObject(gIODisplayParametersCommitKey, commitParams);
        num->release();
        commitParams->release();
                
        if (newDict)
        {
            newDict->merge(backlightParams);
            _display->setProperty(gIODisplayParametersKey, newDict);
            newDict->release();
        }
        else
            _display->setProperty(gIODisplayParametersKey, backlightParams);

        //refresh properties here too
        setProperty(gIODisplayParametersKey, backlightParams);
        
		//backlightParams->release();
        myParams->release();
        return true;
	}
    return false;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma mark -
#pragma mark ACPI related functions
#pragma mark -


bool ACPIBacklightPanel::findDevices(IOService * provider)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    if (!gpuDevice || !backLightDevice)
    {
        IOACPIPlatformDevice * dev = OSDynamicCast(IOACPIPlatformDevice, provider);
        if (hasBacklightMethods(dev))
        {
            DbgLog("%s: PNLF has backlight Methods\n", this->getName());
            backLightDevice = dev;
            gpuDevice = dev;
            gpuDevice->retain();
            backLightDevice->retain();
        }
        else
        {            
            gpuDevice = getGPU();
            
            if (NULL == gpuDevice)
                return false;
            
            gpuDevice->retain();
            
            if (hasBacklightMethods(gpuDevice))
            {
                backLightDevice = gpuDevice;
            }
            else
            {
                backLightDevice = getChildWithBacklightMethods(gpuDevice);
            }
            
            if (backLightDevice == NULL)
                return false;
            
            backLightDevice->retain();
        }
        
        OSBoolean * showPath = OSDynamicCast(OSBoolean, getProperty("Show Device Paths"));
        if (showPath && showPath->isTrue())
        {
            OSString * path;
            if (gpuDevice != backLightDevice)
            {
                OSArray * devicePaths = OSArray::withCapacity(2);
                path = getACPIPath(gpuDevice);
                IOLog("%s: ACPI Method _DOS found. Device path: %s\n", this->getName(), path->getCStringNoCopy());
                devicePaths->setObject(path);
                path->release();
                path = getACPIPath(backLightDevice);
                IOLog("%s: ACPI Methods _BCL _BCM _BQC found. Device path: %s\n", this->getName(), path->getCStringNoCopy());
                devicePaths->setObject(path);
                path->release();
                setProperty("ACPI Devices Paths", devicePaths);
            }
            else
            {
                path = getACPIPath(backLightDevice);
                IOLog("%s: ACPI Methods _DOS _BCL _BCM _BQC found. Device path: %s\n", this->getName(), path->getCStringNoCopy());
                setProperty("ACPI Device Path", path);
                path->release();
            }
        }
        
    }
    return true;
}


#define lgth 512
OSString * ACPIBacklightPanel::getACPIPath(IOACPIPlatformDevice * acpiDevice)
{
    OSString * separator = OSString::withCStringNoCopy(".");
    OSArray * array = OSArray::withCapacity(10);
    
    char devicePath[lgth];
    bzero(devicePath, lgth);
    IOACPIPlatformDevice * parent = acpiDevice;
    
    IORegistryIterator * iter = IORegistryIterator::iterateOver(acpiDevice, IOACPIPlane, kIORegistryIterateParents | kIORegistryIterateRecursively);
    if (iter)
    {
        do {
            array->setObject(parent->copyName(IOACPIPlane));
            array->setObject(separator);
            parent = OSDynamicCast(IOACPIPlatformDevice, iter->getNextObject());
        } while (parent);
        iter->release();
        
        int offset = 0;
        OSString * str = OSDynamicCast(OSString, array->getLastObject());
        for (int i = array->getCount()-2; ((i>=0) || ((offset + str->getLength()) > lgth)) ; i--)
        {
            str = OSDynamicCast(OSString, array->getObject(i));
            strncpy(devicePath + offset, str->getCStringNoCopy(), str->getLength());
            offset += str->getLength();
        }
        array->release();
        separator->release();
    }
    return OSString::withCString(devicePath);
}


IOACPIPlatformDevice *  ACPIBacklightPanel::getGPU()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    IORegistryIterator * iter = IORegistryIterator::iterateOver( IOACPIPlane, kIORegistryIterateRecursively);
    IOACPIPlatformDevice * look = NULL, * ret = NULL;
    IORegistryEntry * entry;
    
    if (iter)
    {
        do
        {
            entry = iter->getNextObject();
            look = OSDynamicCast(IOACPIPlatformDevice, entry);
            if (look)
            {
                DbgLog("%s: testing device: %s\n", this->getName(), look->getName());
                if (hasDOSMethod(look))
                {
                    ret = look;
                    break;
                }
            }
        }
        while (entry) ;
        iter->release();
    }
    return ret;
}


bool ACPIBacklightPanel::hasBacklightMethods(IOACPIPlatformDevice * acpiDevice)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	bool ret = true;
	if (kIOReturnSuccess == acpiDevice->validateObject("_BCL"))
		DbgLog("%s: ACPI device %s has _BCL\n", this->getName(), acpiDevice->getName());
	else
		ret = false;
    
	if (kIOReturnSuccess == acpiDevice->validateObject("_BCM"))
		DbgLog("%s: ACPI device %s has _BCM\n", this->getName(), acpiDevice->getName());
	else
		ret = false;
    
	if (kIOReturnSuccess == acpiDevice->validateObject("_BQC"))
		DbgLog("%s: ACPI device %s has _BQC\n", this->getName(), acpiDevice->getName());	
	else
		ret = false;
	
	return ret;
}


bool ACPIBacklightPanel::hasDOSMethod(IOACPIPlatformDevice * acpiDevice)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	bool ret = true;
	if (kIOReturnSuccess == acpiDevice->validateObject("_DOS"))
		DbgLog("%s: ACPI device %s has _DOS\n", this->getName(), acpiDevice->getName());
	else
		ret = false;
    	
	return ret;
}


bool ACPIBacklightPanel::hasSAVEMethod(IOACPIPlatformDevice * acpiDevice)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	bool ret = true;
	if (kIOReturnSuccess == acpiDevice->validateObject("SAVE"))
		DbgLog("%s: ACPI device %s has SAVE\n", this->getName(), acpiDevice->getName());
	else
		ret = false;
    
	return ret;
}

IOACPIPlatformDevice * ACPIBacklightPanel::getChildWithBacklightMethods(IOACPIPlatformDevice * GPUdevice)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	OSIterator * 		iter = NULL;
	OSObject *		entry;
    
	iter =  GPUdevice->getChildIterator(IOACPIPlane);
	if (iter)
	{
		while ( true )
		{			
			entry = iter->getNextObject();
			if (NULL == entry)
				break;
			
			if (entry->metaCast("IOACPIPlatformDevice"))
			{
				IOACPIPlatformDevice * device = (IOACPIPlatformDevice *) entry;
				
				if (hasBacklightMethods(device))
				{
					IOLog("%s: Found Backlight Device: %s\n", this->getName(), device->getName());
					return device;
				}
			}
			else {
				DbgLog("%s: getChildWithBacklightMethods() Cast Error\n", this->getName());
			}
		} //end while
		iter->release();
		DbgLog("%s: getChildWithBacklightMethods() iterator end\n", this->getName());
	}
	return NULL;
}


OSArray * ACPIBacklightPanel::queryACPISupportedBrightnessLevels()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	OSObject * ret;
	backLightDevice->evaluateObject("_BCL", &ret);
	OSArray * data = OSDynamicCast(OSArray, ret);
	if (data)
	{
		DbgLog("%s: %s _BCL %d\n", this->getName(), backLightDevice->getName(), data->getCount() );
		return data;
	}
	else {
		DbgLog("%s: Cast Error _BCL is %s\n", this->getName(), ret->getMetaClass()->getClassName());
	}
	ret->release();
	return NULL;
}


void ACPIBacklightPanel::setACPIBrightnessLevel(UInt32 level)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	OSObject * ret = NULL;
	OSNumber * number = OSNumber::withNumber(level, 32);
    
	if (kIOReturnSuccess == backLightDevice->evaluateObject("_BCM", &ret, (OSObject**)&number,1))
    {
	if (ret)
		ret->release();
    
	DbgLog("%s: setACPIBrightnessLevel _BCM(%u)\n", this->getName(), (unsigned int) level);
    }
    else
        IOLog("%s: Error in setACPIBrightnessLevel _BCM(%u)\n", this->getName(), (unsigned int) level);
}


void ACPIBacklightPanel::saveACPIBrightnessLevel(UInt32 level)
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	OSObject * ret = NULL;
	OSNumber * number = OSNumber::withNumber(level, 32);
    
	if (kIOReturnSuccess == backLightDevice->evaluateObject("SAVE", &ret, (OSObject**)&number,1))
    {
        if (ret)
            ret->release();
        
        DbgLog("%s: saveACPIBrightnessLevel SAVE(%u)\n", this->getName(), (unsigned int) level);
    }
    else
        IOLog("%s: Error in saveACPIBrightnessLevel SAVE(%u)\n", this->getName(), (unsigned int) level);
}


UInt32 ACPIBacklightPanel::queryACPICurentBrightnessLevel()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	UInt32 level;
	if (kIOReturnSuccess == backLightDevice->evaluateInteger("_BQC", &level))
	{
		DbgLog("%s: queryACPICurentBrightnessLevel _BQC = %d\n", this->getName(), (int) level );
        
        OSBoolean * useIdx = OSDynamicCast(OSBoolean, getProperty("BQC use index"));
        if (useIdx && useIdx->isTrue())
        {
            OSArray * levels = queryACPISupportedBrightnessLevels();
            if (levels)
            {
                OSNumber *num = OSDynamicCast(OSNumber, levels->getObject(level));
                if (num)
                    level = num->unsigned32BitValue();
                levels->release();
            }
        }
        DbgLog("%s: queryACPICurentBrightnessLevel returning %d\n", this->getName(), (int) level );
		return level;
	}
	else {
		IOLog("%s: Error in queryACPICurentBrightnessLevel _BQC\n", this->getName());
	}
    //some laptops didn't return anything on startup, return then max value (first entry in _BCL):
	return minAC;
}


/*
 * Switch from direct hardware controled to software controled mode
 */
void ACPIBacklightPanel::getDeviceControl()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	OSNumber * number = OSNumber::withNumber(0x4, 32); //bit 2 = 1
	OSObject * ret = NULL;
	
	if (kIOReturnSuccess == gpuDevice->evaluateObject("_DOS", &ret, (OSObject**)&number,1))
    {
        if (ret)
            ret->release();
        
        DbgLog("%s: BIOS control disabled: _DOS\n", this->getName());
    }
    else
       IOLog("%s: Error in getDeviceControl _DOS\n", this->getName()); 
}


SInt32 ACPIBacklightPanel::getIndexForLevel(SInt32 BCLvalue)
{
	for (SInt32 i = BCLlevelsCount-1; i>=0 ; i--)
	{
		if (BCLlevels[i] == BCLvalue)
		{
			DbgLog("%s: getIndexForLevel(%d) is %d\n", this->getName(), (int) BCLvalue, (int) i);
			return i;
		}
	}
	IOLog("%s: getIndexForLevel(%d) not found in _BCL table !\n", this->getName(), (int) BCLvalue);
	return 0;
}


SInt32 ACPIBacklightPanel::setupIndexedLevels()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	OSNumber * num;
	OSArray * levels = queryACPISupportedBrightnessLevels();
	if (levels)
	{
		BCLlevelsCount = levels->getCount();
		
		if (BCLlevelsCount < 3)
			return 0;
		
		//verify the types of objects is good once for all
		for (int i = 0; i< BCLlevelsCount; i++) {
			if (!OSDynamicCast(OSNumber, levels->getObject(i)))
				return 0;
		}
		
        //TODO : manage the case when the list has no order! Linux do that
		//test the order of the list
		SInt32 min, max;
		num = OSDynamicCast(OSNumber, levels->getObject(2));
		min = num->unsigned32BitValue();
		num = OSDynamicCast(OSNumber, levels->getObject(BCLlevelsCount-1));
		max = num->unsigned32BitValue();
		
		if (max < min) //list is reverted !
		{
			BCLlevels = IONew(SInt32, BCLlevelsCount);
			for (int i = 0; i< BCLlevelsCount; i++) {
				num = OSDynamicCast(OSNumber, levels->getObject(BCLlevelsCount -1 -i));
				BCLlevels[i] = num->unsigned32BitValue();
			}
		}
		else
		{
			BCLlevelsCount = BCLlevelsCount -2;
			BCLlevels = IONew(SInt32, BCLlevelsCount);
			for (int i = 0; i< BCLlevelsCount; i++) {
				num = OSDynamicCast(OSNumber, levels->getObject(i+2));
				BCLlevels[i] = num->unsigned32BitValue();
			}
		}
		
		//2 first items are min on ac and max on bat
		num = OSDynamicCast(OSNumber, levels->getObject(0));
		minAC = getIndexForLevel(num->unsigned32BitValue());
		setDebugProperty("BCL: Min on AC", num);
		num = OSDynamicCast(OSNumber, levels->getObject(1));
		maxBat = getIndexForLevel(num->unsigned32BitValue());
		setDebugProperty("BCL: Max on Bat", num);
		setProperty("Brightness Control Levels", levels);
		
		return BCLlevelsCount -1;
	}
	return 0;
}


#pragma mark -
#pragma mark AC DC managment for init
#pragma mark -


IOService * ACPIBacklightPanel::getBatteryDevice()
{
	DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
    OSDictionary * matching = IOService::serviceMatching("IOPMPowerSource");
	OSIterator *   iter = NULL;
	IOService * bat = NULL;
	
	if (matching)
	{
		DbgLog("%s: getBatteryDevice() serviceMatching OK\n", this->getName());
		iter = IOService::getMatchingServices(matching);
		matching->release();
	}
	
	if (iter)
	{
		DbgLog("%s: getBatteryDevice() iter OK\n", this->getName());
		
		bat = OSDynamicCast(IOService, iter->getNextObject());
		if (bat)
		{
			DbgLog("%s: getBatteryDevice() bat is of class %s\n", this->getName(), bat->getMetaClass()->getClassName());	
		}
		
		iter->release();
	}
	
	return bat;
}


bool ACPIBacklightPanel::getACStatus()
{
    DbgLog("%s::%s()\n", this->getName(),__FUNCTION__);
    
	IOService * batteryDevice = getBatteryDevice();
	
	if (NULL != batteryDevice)
	{
		OSObject * obj = batteryDevice->getProperty("ExternalConnected");
		OSBoolean * status = OSDynamicCast(OSBoolean, obj);
        if (status)
        {
            DbgLog("%s: getACStatus() AC is %d\n", this->getName(), status->getValue());
            return status->getValue();
        }
        else
            DbgLog("%s: getACStatus() unable to get \"ExternalConnected\" property\n", this->getName());
	}
    return true;
}