#include "SamilCommunicator.h"


SamilCommunicator::SamilCommunicator(SettingsManager * settingsMan, bool inDebug)
{
	settingsManager = settingsMan;
	debugMode = inDebug;
}

void SamilCommunicator::start()
{
	auto settings = settingsManager->GetSettings();
	//create the software serial on the custom pins so we can use the hardware serial for debug comms.
  samilSerial = new SoftwareSerial(settings->RS485Rx, settings->RS485Tx, false); // (RX, TX. inverted, buffer)
	//samilSerial = new SoftwareSerial(settings->RS485Rx, settings->RS485Tx, false, BufferSize); // (RX, TX. inverted, buffer)
	//start the software serial
	samilSerial->begin(9600); //inverter fixed baud rate

	//set the fixed part of our buffer
	headerBuffer[0] = 0x55;
	headerBuffer[1] = 0xAA;
	headerBuffer[2] = SAMIL_COMMS_ADDRES;
  headerBuffer[3] = SAMIL_COMMS_ADDRESS;

	//remove all registered inverters. This is usefull when restarting the ESP. The inverter still thinks it is registered
	//but this program does not know the address. The timeout is 10 minutes.
	//for (char cnt = 1; cnt < 255; cnt++)
	//{
	//	sendRemoveRegistration(cnt);
	//	delay(1);
	//}

	Serial.println("Samil Communicator started.");
}

void SamilCommunicator::stop()
{
	//clear out our data, stop serial.
	inverters.clear();
}


int SamilCommunicator::sendData(unsigned int address, char controlCode, char functionCode, char dataLength, char * data)
{
	if (debugMode)
		Serial.write("Sending data to inverter(s): ");

	//send the header first
  headerBuffer[4] = address >> 8;
  headerBuffer[5] = address & 0xFF;
	headerBuffer[6] = controlCode;
	headerBuffer[7] = functionCode;
	headerBuffer[8] = dataLength;
  //yield();
	delay(500);
	samilSerial->write(headerBuffer, 9);
	//check if we need to write the data part and send it.
	if (dataLength)
		samilSerial->write(data, dataLength);
	//need to send out the crc which is the addition of all previous values.
	uint16_t crc = 0;
	for (int cnt = 0; cnt < 9; cnt++)
	{
		if (debugMode)
			debugPrintHex(headerBuffer[cnt]);
		crc += headerBuffer[cnt];
	}

	for (int cnt = 0; cnt < dataLength; cnt++)
	{
		if (debugMode)
			debugPrintHex(data[cnt]);
		crc += data[cnt];
	}

	//write out the high and low
	auto high = (crc >> 8) & 0xff;
	auto low = crc & 0xff;
	samilSerial->write(high);
	samilSerial->write(low);
	if (debugMode)
	{
		Serial.print("CRC high/low: ");
		debugPrintHex(high);
		debugPrintHex(low);
		Serial.println(".");
	}

	return 9 + dataLength + 2; //header, data, crc
}

void SamilCommunicator::debugPrintHex(char bt)
{
	Serial.print("0x");
	Serial.print(bt, HEX);
	Serial.print(" ");
}

void SamilCommunicator::sendDiscovery()
{
	//send out discovery for unregistered devices.
	if(debugMode)
		Serial.println("Sending discovery");
	sendData(0x00, 0x00, 0x00, 0x00, nullptr);
}

void SamilCommunicator::checkOfflineInverters()
{
	//check inverter timeout
	for (char index = 0; index < inverters.size(); ++index)
	{
		if (inverters[index].isOnline)
		{
			auto newOnline = (millis() - inverters[index].lastSeen < OFFLINE_TIMEOUT);
			
			//check if inverter timed out
			if (!newOnline && inverters[index].isOnline)
			{
				if (debugMode)
				{
					Serial.print("Marking inverter @ address: ");
					Serial.print((short)inverters[index].address);
					Serial.println(" offline.");
				}

//				sendRemoveRegistration(inverters[index].address); //send in case the inverter thinks we are online
			}
			inverters[index].isOnline = newOnline;				
		}		
	}		
}

void SamilCommunicator::checkIncomingData()
{
	if (samilSerial->available())
	{
		while (samilSerial->available() > 0)
		{
			byte incomingData = samilSerial->read();
			
			//wait for packet start. if found read until data length  + data. 
			//set the time we received the data so we can use some kind of timeout
			if (!startPacketReceived && (lastReceivedByte == 0x55 && incomingData == 0xAA))
			{
				//packet start received
				startPacketReceived = true;
				curReceivePtr = 0;
				numToRead = 0;
				lastReceivedByte = 0x00; //reset last received for next packet
			}
			else if (startPacketReceived)
			{
				if (numToRead > 0 || curReceivePtr < 7)
				{
					inputBuffer[curReceivePtr] = incomingData;
					curReceivePtr++;
					if (curReceivePtr == 7)
					{
						//we received the data langth. keep on reading until data length is read.
						//we need to add two for the crc calculation
						numToRead = inputBuffer[6] + 2;
					}
					else if (curReceivePtr > 5)
						numToRead--;


				}
				if (curReceivePtr >= 7 && numToRead == 0)
				{
					//got the complete packet
					//parse it
					startPacketReceived = false;
					parseIncomingData(curReceivePtr);
				}

			}
			else if (!startPacketReceived)
				lastReceivedByte = incomingData; //keep track of the last incoming byte so we detect the packet start
		}

		lastReceived = millis();
	}
	else if (startPacketReceived && millis() - lastReceived > PACKET_TIMEOUT) // 0.5 sec timoeut
	{
		//there is an open packet timeout. 
		startPacketReceived = false; //wait for start packet again
		Serial.println("Comms timeout.");
	}
}
void SamilCommunicator::parseIncomingData(char incomingDataLength) //
{
	//first check the crc
	//Data always start without the start bytes of 0x55 0xAA
	//incomingDataLength also has the crc data in it
	if (debugMode)
	{
		Serial.print("Parsing incoming data with length: ");
		debugPrintHex(incomingDataLength);
		Serial.print(". ");
		debugPrintHex(0x55);
		debugPrintHex(0xAA);
		for (char cnt = 0; cnt < incomingDataLength; cnt++)
			debugPrintHex(inputBuffer[cnt]);
		Serial.println(".");
	}

	uint16_t crc = 0x55 + 0xAA;
	for (char cnt = 0; cnt < incomingDataLength - 2; cnt++)
		crc += inputBuffer[cnt];

	auto high = (crc >> 8) & 0xff;
	auto low = crc & 0xff;

	if (debugMode)
	{
		Serial.print("CRC received: ");
		debugPrintHex(inputBuffer[incomingDataLength - 2]);
		debugPrintHex(inputBuffer[incomingDataLength - 1]);
		Serial.print(", calculated CRC: ");
		debugPrintHex(high);
		debugPrintHex(low);
		Serial.println(".");
	}
	//match the crc
	if (!(high == inputBuffer[incomingDataLength - 2] && low == inputBuffer[incomingDataLength - 1]))
		return;
	if (debugMode)
		Serial.println("CRC match.");
  
	//check the control code and function code to see what to do
  if (inputBuffer[4] == 0x00 && inputBuffer[5] == 0x80) {
    memcpy(serialBuffer, inputBuffer + 7, 10);
    newInverter = true;
  }
  // register new inverter
  else if (inputBuffer[1] == 0xFF && inputBuffer[6] == 0x01 && newInverter) {
    newInverter = false;
    handleRegistration(serialBuffer, 10);
  }
  else if (inputBuffer[4] == 0x00 && inputBuffer[5] == 0x81)
    handleRegistrationConfirmation(inputBuffer[1]);
  else if (inputBuffer[4] == 0x01 && inputBuffer[5] == 0x83)
    handleInverterInformation(inputBuffer[1], inputBuffer[6], inputBuffer + 7);
	else if (inputBuffer[4] == 0x01 && inputBuffer[5] == 0x82)
		handleIncomingInformation(inputBuffer[1], inputBuffer[6], inputBuffer + 7);
}

void SamilCommunicator::handleRegistration(char * serialNumber, char length)
{
	//check if the serialnumber isn't listed yet. If it is use that one
	//Add the serialnumber, generate an address and send it to the inverter
  if (length != 10)
		return;

	for (char index = 0; index < inverters.size(); ++index)
	{
		//check inverter 
    if (memcmp(inverters[index].serialNumber, serialNumber, 10) == 0)
		{
			Serial.print("Already registered inverter reregistered with address: ");
			Serial.println((short)inverters[index].address);
			//found it. Set to unconfirmed and send out the existing address to the inverter
			inverters[index].addressConfirmed = false;
			inverters[index].lastSeen = millis();
			sendAllocateRegisterAddress(serialNumber, inverters[index].address);
			return;
		}
	}

	//still here. This a new inverter
	SamilCommunicator::SamilInverterInformation newInverter;
	newInverter.addressConfirmed = false;
	newInverter.lastSeen = millis();
	newInverter.isDTSeries = false; //TODO. Determine if DT series inverter by getting info
  memset(newInverter.serialNumber, 0, 11);
  memcpy(newInverter.serialNumber, serialNumber, 10);
	//get the new address. Add one (overflows at 255) and check if not in use
	lastUsedAddress++;
	while (getInverterInfoByAddress(lastUsedAddress) != nullptr)
		lastUsedAddress++;
	newInverter.address = lastUsedAddress;
	inverters.push_back(newInverter);
	if (debugMode)
	{
		Serial.print("New inverter found. Current # registrations: ");
		Serial.println(inverters.size());
	}

	sendAllocateRegisterAddress(serialNumber, lastUsedAddress);
}

void SamilCommunicator::handleRegistrationConfirmation(char address)
{
	if (debugMode)
	{
		Serial.print("Handling registration information for address: ");
		Serial.println((short)address);
	}
	//lookup the inverter and set it to confirmed
	auto inverter = getInverterInfoByAddress(address);
	if (inverter)
	{
		if (debugMode)
			Serial.println("Inverter information found in list of inverters.");
		inverter->addressConfirmed = true;
		inverter->isOnline = false; //inverter is online, but we first need to get its information
		inverter->lastSeen = millis();
	}
	else
	{
		if (debugMode)
		{
			Serial.print("Error. Could not find the inverter with address: ");
			Serial.println((short)address);
			Serial.print("Current # registrations: ");
			Serial.println(inverters.size());
		}
	}
	//get the information straight away
	askInverterForType(address);
}

void SamilCommunicator::handleInverterInformation(char address, char dataLength, char * data)
{
  
  auto inverter = getInverterInfoByAddress(address);
  if (inverter == nullptr) return;

  if (dataLength < 44) //minimum for non dt series
    return;

  Serial.println("Inverter data: "); 
  
  if (data[0] == 0x32)
  {
    inverter->isDTSeries = true;
    Serial.println("3-phase inverter");
  }
  else
  {
    inverter->isDTSeries = false;
    Serial.println("1-phase inverter");
  }
    
  Serial.print("Apparent power [VA]: ");
  for (int i = 1; i < 7; i++)
    Serial.print(data[i]);
  Serial.println("");
  
  Serial.print("HMI MCU SW: ");
  for (int i = 7; i < 12; i++)
    Serial.print(data[i]);
  Serial.println("");

  Serial.print("Model name: ");
  for (int i = 12; i < 28; i++)
    Serial.print(data[i]);
  Serial.println("");  

  Serial.print("Manufacturer: ");
  for (int i = 28; i < 44; i++)
    Serial.print(data[i]);
  Serial.println("");

  Serial.print("Serial number: ");
  for (int i = 44; i < 60; i++)
    Serial.print(data[i]);
  Serial.println("");

  askInverterForInformation(address);
     
}

void SamilCommunicator::handleIncomingInformation(char address, char dataLength, char * data)
{
	//need to parse the information and update our struct
	//parse all pairs of two bytes and output them
	auto inverter = getInverterInfoByAddress(address);
	if (inverter == nullptr) return;

	if (dataLength < 44) //minimum for non dt series
		return;

	//data from iniverter, means online
	inverter->lastSeen = millis();
	char dtPtr = 0;
  
  inverter->temp = bytesToFloat(data, 10);                                         dtPtr += 2;
  inverter->vpv1 = bytesToFloat(data + dtPtr, 10);                                dtPtr += 2;
  inverter->vpv2 = bytesToFloat(data + dtPtr, 10);                                dtPtr += 2;
  inverter->ipv1 = bytesToFloat(data + dtPtr, 10);                                dtPtr += 2;
  inverter->ipv2 = bytesToFloat(data + dtPtr, 10);                                dtPtr += 2;
  inverter->eTotal_msw = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]); dtPtr += 2;
  inverter->eTotal_lsw = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]); dtPtr += 2;
  inverter->hTotal_msw = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]); dtPtr += 2;
  inverter->hTotal_lsw = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]); dtPtr += 2;
  inverter->pac = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]);        dtPtr += 2;
  inverter->workMode = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr + 1]);  dtPtr += 2;
  inverter->eDay = bytesToFloat(data + dtPtr, 100);                               dtPtr += 26;
  inverter->pdc1 = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]);       dtPtr += 2;
  inverter->pdc2 = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr +1]);       dtPtr += 2;
  inverter->sinkTemp = bytesToFloat(data + dtPtr, 10);                            dtPtr += 2;
  inverter->iac1 = bytesToFloat(data + dtPtr, 10);                                dtPtr += 2;
  inverter->vac1 = bytesToFloat(data + dtPtr, 10);                                dtPtr += 2;
  inverter->fac1 = bytesToFloat(data + dtPtr, 100);                               dtPtr += 2;
  if (inverter->isDTSeries)
  {
    inverter->iac2 = bytesToFloat(data + dtPtr, 10);                              dtPtr += 2;
    inverter->vac2 = bytesToFloat(data + dtPtr, 10);                              dtPtr += 2;
    inverter->fac2 = bytesToFloat(data + dtPtr, 100);                             dtPtr += 2;
    inverter->iac3 = bytesToFloat(data + dtPtr, 10);                              dtPtr += 2;
    inverter->vac3 = bytesToFloat(data + dtPtr, 10);                              dtPtr += 2;
    inverter->fac3 = bytesToFloat(data + dtPtr, 100);                             dtPtr += 2;
    
  }
  inverter->eTotal = ((float)(((unsigned long)inverter->eTotal_msw << 16) | inverter->eTotal_lsw)) / 10 ;
  inverter->hTotal = ((unsigned long)inverter->hTotal_msw << 16) | inverter->hTotal_lsw;
  
	//isonline is set after first batch of data is set so readers get actual data 
	inverter->isOnline = true;
  inverter->cnt++;
  if (inverter->cnt >= 20)
  {
    Serial.println("PV data:");
    Serial.print("Internal temperature [\xDF""C]: ");
    Serial.println(inverter->temp);
    Serial.print("PV 1 voltage [V]: ");
    Serial.println(inverter->vpv1);
    Serial.print("PV 2 voltage [V]: ");
    Serial.println(inverter->vpv2);
    Serial.print("PV 1 current [A]: ");
    Serial.println(inverter->ipv1);
    Serial.print("PV 2 current [A]: ");
    Serial.println(inverter->ipv2);
    /*
    Serial.println(inverter->eTotal_msw);
    Serial.println(inverter->eTotal_lsw);
    Serial.println(inverter->hTotal_msw);
    Serial.println(inverter->hTotal_lsw);
    */
    Serial.print("Inverter operation mode (0=wait, 1=normal, 2=fault, 3=permanent fault, 4=check, 5=PV power off): ");
    Serial.println(inverter->workMode);
    Serial.print("Output power AC [W]: ");
    Serial.println(inverter->pac);
    Serial.print("Day energy prod. [kWh]: ");
    Serial.println(inverter->eDay);
    Serial.print("PV 1 power [W]: ");
    Serial.println(inverter->pdc1);
    Serial.print("PV 2 power [W]: ");
    Serial.println(inverter->pdc2);
    Serial.print("Heat sink temperature [\xDF""C]: ");
    Serial.println(inverter->sinkTemp);
    Serial.print("AC phase 1 current [A]: ");
    Serial.println(inverter->iac1);
    Serial.print("AC phase 1 voltage [V]: ");
    Serial.println(inverter->vac1);
    Serial.print("AC phase 1 frequency [Hz]: ");
    Serial.println(inverter->fac1);
    Serial.print("AC phase 2 current [A]: ");
    Serial.println(inverter->iac2);
    Serial.print("AC phase 2 voltage [V]: ");
    Serial.println(inverter->vac2);
    Serial.print("AC phase 2 frequency [Hz]: ");
    Serial.println(inverter->fac2);
    Serial.print("AC phase 3 current [A]: ");
    Serial.println(inverter->iac3);
    Serial.print("AC phase 3 voltage [V]: ");
    Serial.println(inverter->vac3);
    Serial.print("AC phase 3 frequency [Hz]: ");
    Serial.println(inverter->fac3);
    Serial.print("Energy total [kWh]: ");
    Serial.println(inverter->eTotal);
    Serial.print("Total operation time [h]: ");
    Serial.println(inverter->hTotal);
    inverter->cnt = 0;
  }
  
  
}

float SamilCommunicator::bytesToFloat(char * bt, char factor)
{
	//convert two byte to float by converting to short and then dividing it by factor
	return float(((unsigned short)bt[0] << 8) | bt[1]) / factor;
}

void SamilCommunicator::askAllInvertersForInformation()
{
	for (char index = 0; index < inverters.size(); ++index)
	{
		if (inverters[index].addressConfirmed && inverters[index].isOnline)
			askInverterForInformation(inverters[index].address);
		else
		{
			if (debugMode)
			{
				Serial.print("Not asking inverter with address: ");
				Serial.print((short)inverters[index].address);
				Serial.print(" for information. Addressconfirmed: ");
				Serial.print((short)inverters[index].addressConfirmed);
				Serial.print(", isOnline: ");
				Serial.print((short)inverters[index].isOnline);
				Serial.println(".");
			}
		}
	}
}

void SamilCommunicator::askInverterForType(char address)
{
  sendData(address, 0x01, 0x03, 0, nullptr);
}

void SamilCommunicator::askInverterForInformation(char address)
{
	sendData(address, 0x01, 0x02, 0, nullptr);
}

SamilCommunicator::SamilInverterInformation *  SamilCommunicator::getInverterInfoByAddress(char address)
{
	for (char index = 0; index < inverters.size(); ++index)
	{
		//check inverter 
		if (inverters[index].address == address)
			return &inverters[index];
	}
	return nullptr;
}

void SamilCommunicator::sendAllocateRegisterAddress(char * serialNumber, char address)
{
	if (debugMode)
	{
		Serial.print("SendAllocateRegisterAddress address: ");
		Serial.println((short)address);
	}

	//create our registrationpacket with serialnumber and address and send it over
	char RegisterData[11];
	//memcpy(RegisterData, serialNumber, 16);
  memcpy(RegisterData, serialNumber, 10);
  RegisterData[10] = address;
	//need to send alloc msg
	sendData(0x00, 0x00, 0x01, 11, RegisterData);
}

//void SamilCommunicator::sendRemoveRegistration(char address)
//{
	//send out the remove address to the inverter. If the inverter is still connected it will reconnect after discovery
//	sendData(address, 0x00, 0x02, 0, nullptr);
//}
void SamilCommunicator::handle()
{
	//always check for incoming data
	checkIncomingData();

	//check for offline inverters
	checkOfflineInverters();

	//discovery every 10 secs.
	if (millis() - lastDiscoverySent >= DISCOVERY_INTERVAL)
	{
		sendDiscovery();
		lastDiscoverySent = millis();
	}

	//ask for info update every second
	if (millis() - lastInfoUpdateSent >= 1000)
	{
		askAllInvertersForInformation();
		lastInfoUpdateSent = millis();
	}
	checkIncomingData();
}


std::vector<SamilCommunicator::SamilInverterInformation> SamilCommunicator::getInvertersInfo()
{
	return inverters;
}

SamilCommunicator::~SamilCommunicator()
{
}
