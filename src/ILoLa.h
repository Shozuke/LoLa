// ILoLa.h

#ifndef _ILOLA_h
#define _ILOLA_h

#define DEBUG_LOLA
//#define MOCK_RADIO

#if !defined(UINT16_MAX) || !defined(INT16_MIN) || !defined(UINT32_MAX) || defined(UINT8_MAX)
#include <stdint.h>
#endif

#define ILOLA_DEFAULT_CHANNEL				0
#define ILOLA_DEFAULT_TRANSMIT_POWER		10
#define ILOLA_DEFAULT_DUPLEX_PERIOD_MILLIS	10

#define ILOLA_DEFAULT_MIN_RSSI				(int16_t(-100))

#define ILOLA_INVALID_RSSI					((int16_t)INT16_MIN)
#define ILOLA_INVALID_MILLIS				((uint32_t)UINT32_MAX)
#define ILOLA_INVALID_MICROS				ILOLA_INVALID_MILLIS
#define ILOLA_INVALID_LATENCY				((uint16_t)UINT16_MAX)

#include <Arduino.h>
#include <Packet\LoLaPacket.h>
#include <Packet\LoLaPacketMap.h>
#include <Crypto\UniqueIdProvider.h>
#include <Crypto\ISeedSource.h>

#include <ClockSource.h>


class ILinkActiveIndicator
{
public:
	virtual bool HasLink() { return false; }
};

class ILoLa
{
protected:
	class IncomingInfoStruct
	{
	private:
		uint32_t PacketTime = ILOLA_INVALID_MILLIS;
		int16_t PacketRSSI = ILOLA_INVALID_RSSI;

	public:
		IncomingInfoStruct() {}

		uint32_t GetPacketTime()
		{
			return PacketTime;
		}

		int16_t GetPacketRSSI()
		{
			return PacketRSSI;
		}

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

protected:
	///Statistics
	volatile uint32_t LastSent = ILOLA_INVALID_MILLIS;
	volatile uint32_t LastReceived = ILOLA_INVALID_MILLIS;
	volatile int16_t LastReceivedRssi = ILOLA_INVALID_RSSI;

	uint32_t LastValidReceived = ILOLA_INVALID_MILLIS;
	int16_t LastValidReceivedRssi = ILOLA_INVALID_RSSI;
	///

	///Configurations
	uint8_t TransmitPower = ILOLA_DEFAULT_TRANSMIT_POWER;
	uint8_t CurrentChannel = ILOLA_DEFAULT_CHANNEL;
	bool Enabled = false;
	const uint8_t DuplexPeriodMillis = ILOLA_DEFAULT_DUPLEX_PERIOD_MILLIS;
	///

	///Status
	ILinkActiveIndicator* LinkIndicator = nullptr;
	bool EvenSlot = false;
	///

	///Packet Mapper for known definitions.
	LoLaPacketMap PacketMap;
	///

	///Synced clock
	ClockSource SyncedClock;
	///Unique Id
	UniqueIdProvider IdProvider;
	///

public:
	ILoLa() : IdProvider()
	{
	}

	uint8_t GetIdLength() const
	{
		return IdProvider.GetUUIDLength();
	}

	uint8_t* GetIdPointer()
	{
		return IdProvider.GetUUIDPointer();
	}

	void Enable()
	{
		if (!Enabled)
		{
			OnStart();
		}
		Enabled = true;
	}

	void Disable()
	{
		if (Enabled)
		{
			OnStop();
		}
		Enabled = false;
	}

	void SetLinkIndicator(ILinkActiveIndicator * indicator)
	{
		LinkIndicator = indicator;
	}

	void SetDuplexSlot(const bool evenSlot)
	{
		EvenSlot = evenSlot;
	}

	bool IsLinkActive()
	{
		if (LinkIndicator != nullptr)
		{
			return LinkIndicator->HasLink();
		}
		else
		{
			return false;
		}
	}

	ClockSource* GetClockSource()
	{
		return &SyncedClock;
	}


	uint32_t GetLastSentMillis()
	{
		return LastSent;
	}

	uint32_t GetLastReceivedMillis()
	{
		return LastReceived;
	}

	int16_t GetLastRSSI()
	{
		return LastReceivedRssi;
	}

	uint32_t GetLastValidReceivedMillis()
	{
		return  LastValidReceived;
	}

	int16_t GetLastValidRSSI()
	{
		return LastValidReceivedRssi;
	}

	uint8_t GetTransmitPower()
	{
		return TransmitPower;
	}

	bool SetTransmitPower(const uint8_t transmitPower)
	{
		TransmitPower = transmitPower;

		OnTransmitPowerUpdated();

		return true;
	}

	uint8_t GetChannel()
	{
		return CurrentChannel;
	}

	bool SetChannel(const int16_t channel)
	{
		CurrentChannel = channel;

		OnChannelUpdated();

		return true;
	}

	LoLaPacketMap * GetPacketMap()
	{
		return &PacketMap;
	}

	inline uint32_t GetMillis()
	{
		return millis();
	}

	inline uint32_t GetMillisSync()
	{
		return SyncedClock.GetMillis();
	}

protected:
	uint8_t * GetTransmitPowerPointer()
	{
		return &TransmitPower;
	}

	uint8_t* GetChannelPointer()
	{
		return &CurrentChannel;
	}

protected:
	virtual void OnChannelUpdated() {}
	virtual void OnTransmitPowerUpdated() {}

public:
	virtual bool SendPacket(ILoLaPacket* packet) { return false; }
	virtual bool Setup() { return true; }
	virtual bool AllowedSend(const bool overridePermission = false) { return true; }
	virtual void OnStart() {}
	virtual void OnStop() {}

	virtual uint8_t GetTransmitPowerMax() const { return 0xFF; }
	virtual uint8_t GetTransmitPowerMin() const { return 0; }
	virtual int16_t GetRSSIMax() const { return 0; }
	virtual int16_t GetRSSIMin() const { return ILOLA_DEFAULT_MIN_RSSI; }
	virtual uint8_t GetChannelMax() const { return 100; }
	virtual uint8_t GetChannelMin() const { return 0; }

	virtual void SetCryptoSeedSource(ISeedSource* cryptoSeedSource) {}

	virtual void OnWakeUpTimer() {}
	virtual void OnReceiveBegin(const uint8_t length, const  int16_t rssi) {}
	virtual void OnReceivedFail(const int16_t rssi) {}
	virtual void OnReceived() {}
	virtual void OnSentOk() {}
	virtual void OnIncoming(const int16_t rssi) {}

	virtual void OnBatteryAlarm() {}

#ifdef DEBUG_LOLA
	virtual void Debug(Stream* serial)
	{
		PacketMap.Debug(serial);
	}
#endif
};
#endif

