// LoLaPacketDriver.h

#ifndef _LOLAPACKETDRIVER_h
#define _LOLAPACKETDRIVER_h

#include <ILoLa.h>
#include <Packet\LoLaPacketMap.h>
#include <Transceivers\LoLaReceiver.h>
#include <Transceivers\LoLaSender.h>

#include <Services\LoLaServicesManager.h>

#include <PacketDriver\AsyncActionCallback.h>

#define LOLA_PACKET_MANAGER_SEND_MIN_BACK_OFF_DURATION_MILLIS				(uint32_t)5
#define LOLA_PACKET_MANAGER_SEND_AFTER_RECEIVE_MIN_BACK_OFF_DURATION_MILLIS (uint32_t)5

class LoLaPacketDriver : public ILoLa
{
private:
	//Helper for IsInSendSlot().
	uint32_t SendSlotElapsed;

protected:
	///Services that are served receiving packets.
	LoLaServicesManager Services;
	///

	bool SetupOk = false;
	LoLaReceiver Receiver;
	LoLaSender Sender;

	//Async handler for interrupt triggered events.
	enum AsyncActionsEnum : uint8_t
	{
		ActionFireOnReceived,
		ActionFireBatteryAlarm,
		ActionFireWakeUpTimer,
		ActionFireOnSentOk
	};

	AsyncActionCallback EventQueue;

public:
	LoLaPacketDriver(Scheduler* scheduler);
	LoLaServicesManager* GetServices();
	void SetCryptoSeedSource(ISeedSource* cryptoSeedSource);

	void OnAsyncEvent(const uint8_t actionCode);

	void FireOnSentOk();
	void FireOnReceived();
	void FireBatteryAlarm();
	void FireWakeUpTimer();

public:
	virtual bool SendPacket(ILoLaPacket* packet);
	virtual bool Setup();
	virtual void OnIncoming(const int16_t rssi);
	virtual void OnReceiveBegin(const uint8_t length, const int16_t rssi);
	virtual void OnReceivedFail(const int16_t rssi);
	virtual void OnSentOk();
	virtual void OnReceived();

	virtual bool AllowedSend(const bool overridePermission = false);

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
	virtual void OnStart() {}

protected:
	void OnBatteryAlarm();
	void OnWakeUpTimer();

private:
	inline bool HotAfterSend();
	inline bool HotAfterReceive();

	inline bool IsInSendSlot();
};
#endif