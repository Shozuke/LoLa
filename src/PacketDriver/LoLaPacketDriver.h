// LoLaPacketDriver.h

#ifndef _LOLAPACKETDRIVER_h
#define _LOLAPACKETDRIVER_h

#include <ILoLa.h>
#include <Packet\LoLaPacketMap.h>
#include <Transceivers\LoLaReceiver.h>
#include <Transceivers\LoLaSender.h>

#include <Services\LoLaServicesManager.h>

#include <PacketDriver\AsyncActionCallback.h>

#define _TASK_OO_CALLBACKS
#include <TaskSchedulerDeclarations.h>

#define LOLA_PACKET_MANAGER_SEND_MIN_BACK_OFF_DURATION_MILLIS 3
#define LOLA_PACKET_MANAGER_SEND_AFTER_RECEIVE_MIN_BACK_OFF_DURATION_MILLIS 5

class LoLaPacketDriver : public ILoLa
{
private:
	struct IncomingInfoStruct
	{
		uint32_t PacketTime = ILOLA_INVALID_MILLIS;
		int16_t PacketRSSI = ILOLA_INVALID_RSSI;

		void Clear()
		{
			PacketTime = ILOLA_INVALID_MILLIS;
			PacketRSSI = ILOLA_INVALID_RSSI;
		}
		bool HasInfo()
		{
			return PacketTime != ILOLA_INVALID_MILLIS && PacketRSSI != ILOLA_INVALID_RSSI;
		}
		void SetInfo(const uint32_t time, const int16_t rssi)
		{
			PacketTime = time;
			PacketRSSI = rssi;
		}
	} IncomingInfo;

	//Helper for IsInSendSlot().
	uint32_t SendSlotElapsed;

protected:
	///Services that are served receiving packets.
	LoLaServicesManager Services;
	///

	uint32_t LastValidReceived = ILOLA_INVALID_MILLIS;
	int16_t LastValidReceivedRssi = ILOLA_INVALID_RSSI;
	bool SetupOk = false;
	LoLaReceiver Receiver;
	LoLaSender Sender;

	//Async handler for interrupt triggered events.
	enum AsyncActionsEnum : uint8_t
	{
		Receive,
		Check,
		BatteryAlarm,
		WakeUpTimer
	};

	AsyncActionCallback EventQueue;

public:
	LoLaPacketDriver(Scheduler* scheduler);
	LoLaServicesManager* GetServices();
	uint32_t GetLastValidReceivedMillis();
	int16_t GetLastValidRSSI();
	void SetCryptoSeedSource(ISeedSource* cryptoSeedSource);

	void OnAsyncEvent(const uint8_t actionCode);

public:
	virtual bool Setup();
	virtual bool AllowedSend(const bool overridePermission = false);
	virtual bool SendPacket(ILoLaPacket* packet);

	//Public calls for interrupts.
	virtual void OnIncoming(const int16_t rssi);
	virtual void OnReceiveBegin(const uint8_t length, const int16_t rssi);
	virtual void OnReceivedFail(const int16_t rssi);
	virtual void OnSentOk();
	virtual void OnBatteryAlarm();
	virtual void OnWakeUpTimer();

#ifdef DEBUG_LOLA
	virtual void Debug(Stream* serial)
	{
		ILoLa::Debug(serial);
		Services.Debug(serial);
	}
#endif

protected:
	virtual bool Transmit() { return false; }
	virtual bool CanTransmit() { return true; }

	virtual void ReceivePacket();
	virtual void WakeUp() {};
	virtual void BatteryAlarmed() {};
	virtual void CheckForPending() {};

	virtual void OnStart();

protected:
	void CheckForPendingAsync();

private:
	inline bool HotAfterSend();
	inline bool HotAfterReceive();

	inline bool IsInSendSlot();
};
#endif