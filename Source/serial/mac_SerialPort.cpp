//mac_SerialPort.cpp
//Serial Port classes in a Juce stylee, by graffiti

//TODO: Broadcast incoming serial messages!

#include "mac_SerialPort.h"

StringPairArray SerialPort::getSerialPortPaths()
{
	StringPairArray SerialPortPaths;
	io_iterator_t matchingServices;
	mach_port_t         masterPort;
    CFMutableDictionaryRef  classesToMatch;
	io_object_t     modemService;
	char deviceFilePath[512];
	char deviceFriendly[1024];
    if (KERN_SUCCESS != IOMasterPort(MACH_PORT_NULL, &masterPort))
    {
        DBG("SerialPort::getSerialPortPaths : IOMasterPort failed");
		return SerialPortPaths;
    }
    classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
    if (classesToMatch == NULL)
	{
		DBG("SerialPort::getSerialPortPaths : IOServiceMatching failed");
		return SerialPortPaths;
	}
	CFDictionarySetValue(classesToMatch, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDRS232Type));    
	if (KERN_SUCCESS != IOServiceGetMatchingServices(masterPort, classesToMatch, &matchingServices))
	{
		DBG("SerialPort::getSerialPortPaths : IOServiceGetMatchingServices failed");
		return SerialPortPaths;
	}
	while ((modemService = IOIteratorNext(matchingServices)))
	{
		CFTypeRef   deviceFilePathAsCFString;
		CFTypeRef   deviceFriendlyAsCFString;
		deviceFilePathAsCFString = IORegistryEntryCreateCFProperty(modemService,CFSTR(kIODialinDeviceKey), kCFAllocatorDefault, 0);
		deviceFriendlyAsCFString = IORegistryEntryCreateCFProperty(modemService,CFSTR(kIOTTYDeviceKey), kCFAllocatorDefault, 0);
		if(deviceFilePathAsCFString)
		{
			if(CFStringGetCString((const __CFString*)deviceFilePathAsCFString, deviceFilePath, 512, kCFStringEncodingASCII)
			&& CFStringGetCString((const __CFString*)deviceFriendlyAsCFString, deviceFriendly, 1024, kCFStringEncodingASCII) )
				SerialPortPaths.set(deviceFriendly, deviceFilePath);
			CFRelease(deviceFilePathAsCFString);
			CFRelease(deviceFriendlyAsCFString);
		}
	}
	IOObjectRelease(modemService);
	return SerialPortPaths;
}
bool SerialPort::exists()
{
	return (-1!=portDescriptor);
}
void SerialPort::close()
{
	if(-1 != portDescriptor)
	{
		//wait for garbage to go? nah...
		//tcdrain(portDescriptor);
		::close(portDescriptor);
		portDescriptor = -1;
	}
}
bool SerialPort::open(const String & portPath)
{
	this->portPath = portPath;
    struct termios options;
	portDescriptor = ::open((const char*)portPath.getCharPointer(), O_RDWR | O_NOCTTY | O_NONBLOCK); //changed .toCString() (which I added) to .getCharPointer()
    if (portDescriptor == -1)
    {
        DBG("SerialPort::open : open() failed");
        return false;
    }
    // don't allow multiple opens
    if (ioctl(portDescriptor, TIOCEXCL) == -1)
    {
        DBG("SerialPort::open : ioctl error, non critical");
    }
    // we want blocking io actually
	if (fcntl(portDescriptor, F_SETFL, 0) == -1)
    {
        DBG("SerialPort::open : fcntl error");
		close();
        return false;
    }
	// Get the current options
    if (tcgetattr(portDescriptor, &options) == -1)
    {
        DBG("SerialPort::open : can't get port settings to set timeouts");
		close();
        return false;
    }
	//non canocal, 0.5 second timeout, read returns as soon as any data is recieved
	cfmakeraw(&options);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 5;
	if (tcsetattr(portDescriptor, TCSANOW, &options) == -1)
    {
        DBG("SerialPort::open : can't set port settings (timeouts)");
		close();
        return false;
    }
	return true;
}
bool SerialPort::setConfig(const SerialPortConfig & config)
{
	if(-1==portDescriptor)return false;
	struct termios options;
	memset(&options, 0, sizeof(struct termios));
	//non canocal, 0.5 second timeout, read returns as soon as any data is recieved
	cfmakeraw(&options);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 5;
	options.c_cflag |= CREAD; //enable reciever (daft)
	options.c_cflag |= CLOCAL;//don't monitor modem control lines
	//baud and bits
	cfsetspeed(&options, config.bps);
	switch(config.databits)
	{
		case 5: options.c_cflag |= CS5; break;
		case 6: options.c_cflag |= CS6; break;
		case 7: options.c_cflag |= CS7; break;
		case 8: options.c_cflag |= CS8; break;
	}
	//parity
	switch(config.parity)
	{
	case SerialPortConfig::SERIALPORT_PARITY_ODD:
		options.c_cflag |= PARENB;
		options.c_cflag |= PARODD;
		break;
	case SerialPortConfig::SERIALPORT_PARITY_EVEN:
		options.c_cflag |= PARENB;
		break;
	case SerialPortConfig::SERIALPORT_PARITY_MARK:
	case SerialPortConfig::SERIALPORT_PARITY_SPACE:
		DBG("SerialPort::setConfig : SERIALPORT_PARITY_MARK and SERIALPORT_PARITY_SPACE not supported on Mac");
		return false;//not supported
		break;
	case SerialPortConfig::SERIALPORT_PARITY_NONE:
	default:
		break;
	}
	//stopbits
	if(config.stopbits==SerialPortConfig::STOPBITS_1ANDHALF)
	{
		DBG("SerialPort::setConfig : STOPBITS_1ANDHALF not supported on Mac");
		return false;//not supported
	}
	if(config.stopbits==SerialPortConfig::STOPBITS_2)
		options.c_cflag |= CSTOPB;
	//flow control
	switch(config.flowcontrol)
	{
	case SerialPortConfig::FLOWCONTROL_XONXOFF:
		options.c_iflag |= IXON;
		options.c_iflag |= IXOFF;
		break;
	case SerialPortConfig::FLOWCONTROL_HARDWARE:
		options.c_cflag |= CCTS_OFLOW;
		options.c_cflag |= CRTS_IFLOW;
		break;
	case SerialPortConfig::FLOWCONTROL_NONE:
	default:
		break;
	}
	if (tcsetattr(portDescriptor, TCSANOW, &options) == -1)
    {
        DBG("SerialPort::setConfig : can't set port settings");
        return false;
    }
	return true;
}
bool SerialPort::getConfig(SerialPortConfig & config)
{
	struct termios options;
	if(-1==portDescriptor)return false;
	if (tcgetattr(portDescriptor, &options) == -1)
    {
        DBG("SerialPort::getConfig : cannot get port settings");
        return false;
    }
	config.bps = ((int)cfgetispeed(&options))>((int)cfgetospeed(&options))?(int)cfgetispeed(&options):(int)cfgetospeed(&options);
	switch(options.c_cflag & CSIZE)
	{
	case CS5: config.databits=5; break;
	case CS6: config.databits=6; break;
	case CS7: config.databits=7; break;
	case CS8: config.databits=8; break;
	}
	config.parity = SerialPortConfig::SERIALPORT_PARITY_NONE;
	if(options.c_cflag & PARENB)
	{ 
		if(options.c_cflag & PARODD)config.parity = SerialPortConfig::SERIALPORT_PARITY_ODD;
		else config.parity = SerialPortConfig::SERIALPORT_PARITY_EVEN;
	}
	//stopbits
	config.stopbits = SerialPortConfig::STOPBITS_1;
	if(options.c_cflag & CSTOPB)config.stopbits = SerialPortConfig::STOPBITS_2;
	//flow control
	config.flowcontrol=SerialPortConfig::FLOWCONTROL_NONE;
	if((options.c_iflag & IXON) || (options.c_iflag & IXOFF))
		config.flowcontrol=SerialPortConfig::FLOWCONTROL_XONXOFF;
	else if((options.c_cflag & CCTS_OFLOW) || (options.c_cflag & CRTS_IFLOW))
		config.flowcontrol=SerialPortConfig::FLOWCONTROL_HARDWARE;
	
	return true;
}
/////////////////////////////////
// SerialPortInputStream
/////////////////////////////////
void SerialPortInputStream::run()
{
	while(port && (port->portDescriptor!=-1) && !threadShouldExit())
	{
        unsigned char c;
		int bytesread=0;
		bytesread = ::read(port->portDescriptor, &c, 1);
		if(bytesread==1)
		{
			const ScopedLock l(bufferCriticalSection);
			buffer.ensureSize(bufferedbytes+1);
			((unsigned char *)buffer.getData())[bufferedbytes]=c;  //added.getData()
			bufferedbytes++;
			if(notify==NOTIFY_ALWAYS||((notify==NOTIFY_ON_CHAR) && (c == notifyChar)))
            {
                //sendChangeMessage(this); //changed from sendChangeMessage to unpacking and sending String below...                
                String serialMessage;
                while(canReadLine()){
                    serialMessage = readNextLine();
                    sensorRecorderListeners.call(&SensorRecorder::myCallbackMethod,&serialMessage);
                }                
            }
		}
	}
}
int SerialPortInputStream::read(void *destBuffer, int maxBytesToRead)
{
	const ScopedLock l(bufferCriticalSection);
	if(maxBytesToRead>bufferedbytes)maxBytesToRead=bufferedbytes;
	memcpy(destBuffer, (void*)buffer.getData(), maxBytesToRead);  //added.getData()
	buffer.removeSection(0,maxBytesToRead);
	bufferedbytes-=maxBytesToRead;
	return maxBytesToRead;
}
/////////////////////////////////
// SerialPortOutputStream
/////////////////////////////////
void SerialPortOutputStream::run()
{
	unsigned char tempbuffer[writeBufferSize];
	while(port && (port->portDescriptor!=-1) && !threadShouldExit())
	{
		if(!bufferedbytes)
			triggerWrite.wait(100);
		if(bufferedbytes)
		{
			int byteswritten=0;
			bufferCriticalSection.enter();
			int bytestowrite=bufferedbytes>writeBufferSize?writeBufferSize:bufferedbytes;
			memcpy(tempbuffer, (void*)buffer.getData(), bytestowrite); //added .getData()
			bufferCriticalSection.exit();
			byteswritten = ::write(port->portDescriptor, tempbuffer, bytestowrite);
			if(byteswritten>0)
			{
				const ScopedLock l(bufferCriticalSection);
				buffer.removeSection(0, byteswritten);
				bufferedbytes-=byteswritten;
			}
		}
	}
}
bool SerialPortOutputStream::write(const void *dataToWrite, int howManyBytes)
{
	bufferCriticalSection.enter();
	buffer.ensureSize(bufferedbytes+howManyBytes);
	memcpy((char*)buffer.getData()+bufferedbytes, dataToWrite, howManyBytes); ///added .getData()
	bufferedbytes+=howManyBytes;
	bufferCriticalSection.exit();
	triggerWrite.signal();
	return true;
}
