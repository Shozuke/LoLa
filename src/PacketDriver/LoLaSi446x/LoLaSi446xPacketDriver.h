// LoLaSi446xPacketDriver.h

#ifndef _LOLASI446XPACKETDRIVER_h
#define _LOLASI446XPACKETDRIVER_h

#define _TASK_OO_CALLBACKS

#include <Arduino.h>

#include <PacketDriver\LoLaPacketDriver.h>
#include <RingBufCPP.h>

#include <PacketDriver\AsyncActionCallback.h>

#include <SPI.h>


#ifndef MOCK_RADIO
#include <Si446x.h>
#endif // !MOCK_RADIO


//Channel to listen to(0 - 255)
#define CHANNEL 100


#define TRANSMIT_POWER 12

//   0 = -32dBm	(<1uW)
//   7 =  0dBm	(1mW)
//  12 =  5dBm	(3.2mW)
//  22 =  10dBm	(10mW)
//  40 =  15dBm	(32mW)
// 100 = 20dBm	(100mW) Requires Dual Antennae
// 127 = ABSOLUTE_MAX
#define SI4463_MAX_TRANSMIT_POWER 40

#define PART_NUMBER_SI4463X 17507

#define SI4463_MIN_RSSI (int16_t(-110))
#define SI4463_MAX_RSSI (int16_t(-50))

class LoLaSi446xPacketDriver : public LoLaPacketDriver
{
protected:
	enum AsyncActionsEnum : uint8_t
	{
		Receive,
		Check,
		BatteryAlarm,
		WakeUpTimer
	};

private:
	AsyncActionCallback EventQueue;

protected:
	bool Transmit();
	bool CanTransmit();
	void OnStart();

private:
	void CheckForPendingAsync();
	void DisableInterruptsInternal();

public:
	LoLaSi446xPacketDriver(Scheduler* scheduler);
	bool Setup();
	void CheckForPending();
	bool DisableInterrupts();

	void EnableInterrupts();

	void OnWakeUpTimer();
	void OnReceiveBegin(const uint8_t length, const  int16_t rssi);

	void OnReceivedFail(const int16_t rssi);
	void OnReceived();

	uint8_t GetTransmitPowerMax();
	uint8_t GetTransmitPowerMin();

	int16_t GetRSSIMax();
	int16_t GetRSSIMin();

	void OnAsyncEvent(const uint8_t actionCode);
};
#endif