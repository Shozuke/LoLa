// LoLaSi446xPacketDriver.h

#ifndef _LOLASI446XPACKETDRIVER_h
#define _LOLASI446XPACKETDRIVER_h

#define _TASK_OO_CALLBACKS

#include <Arduino.h>
#include <TaskSchedulerDeclarations.h>
#include <PacketDriver\LoLaPacketDriver.h>
#include <RingBufCPP.h>
#include <SPI.h>

#ifndef MOCK_RADIO
#include <Si446x.h>
#endif // !MOCK_RADIO


#define PART_NUMBER_SI4463X (uint32_t)17507

//   0 = -32dBm	(<1uW)
//   7 =  0dBm	(1mW)
//  12 =  5dBm	(3.2mW)
//  22 =  10dBm	(10mW)
//  40 =  15dBm	(32mW)
// 100 = 20dBm	(100mW) Requires Dual Antennae
// 127 = ABSOLUTE_MAX
#define SI4463_TRANSMIT_POWER_MIN 1
#define SI4463_TRANSMIT_POWER_MAX 40

//Channel to listen to(0 - 20)
#define SI4463_CHANNEL_MIN	0
#define SI4463_CHANNEL_MAX	10

#define SI4463_RSSI_MIN		(int16_t(-120))
#define SI4463_RSSI_MAX		(int16_t(-50))

class LoLaSi446xPacketDriver : public LoLaPacketDriver
{
protected:
	bool Transmit();
	bool CanTransmit();
	void OnStart();
	void OnChannelUpdated();
	void OnTransmitPowerUpdated();

public:
	LoLaSi446xPacketDriver(Scheduler* scheduler);
	bool Setup();

	void OnReceiveBegin(const uint8_t length, const  int16_t rssi);
	void OnReceivedFail(const int16_t rssi);

	//Override to capture data before processing in base class.
	void OnReceived();

	uint8_t GetChannelMax() const
	{
		return SI4463_CHANNEL_MAX;
	}

	uint8_t GetChannelMin() const
	{
		return SI4463_CHANNEL_MIN;
	}

	uint8_t GetTransmitPowerMax() const
	{
		return SI4463_TRANSMIT_POWER_MAX;
	}

	uint8_t GetTransmitPowerMin() const
	{
		return SI4463_TRANSMIT_POWER_MIN;
	}

	int16_t GetRSSIMax() const
	{
		return SI4463_RSSI_MAX;
	}

	int16_t GetRSSIMin() const
	{
		return SI4463_RSSI_MIN;
	}

};
#endif