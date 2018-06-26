// AbstractSurface.h

#ifndef _ABSTRACTSURFACE_h
#define _ABSTRACTSURFACE_h

#include <Arduino.h>
#include <Crypto\TinyCRC.h>
#include <Services\IPacketSendService.h>
#include <Services\SyncSurface\ITrackedSurface.h>

#define ABSTRACT_SURFACE_DEFAULT_HASH 0

#define ABSTRACT_SURFACE_MAX_ELAPSED_BEFORE_SYNC_RESTART	1000
#define ABSTRACT_SURFACE_MAX_ELAPSED_BEFORE_RESYNC_DEMOTE	500
#define ABSTRACT_SURFACE_SYNC_REPLY_TIMEOUT					100

#define ABSTRACT_SURFACE_SYNC_PERSISTANCE_COUNT				4
#define ABSTRACT_SURFACE_SYNC_KEEP_ALIVE_MILLIS				3000

#define ABSTRACT_SURFACE_SYNC_RETRY_PERIDO					(ABSTRACT_SURFACE_SYNC_REPLY_TIMEOUT/ABSTRACT_SURFACE_SYNC_PERSISTANCE_COUNT)
#define ABSTRACT_SURFACE_SYNC_PERSISTANCE_PERIOD			(ABSTRACT_SURFACE_SYNC_KEEP_ALIVE_MILLIS/ABSTRACT_SURFACE_SYNC_PERSISTANCE_COUNT)

class AbstractSync : public IPacketSendService
{
private:
	uint8_t LastRemoteHash = ABSTRACT_SURFACE_DEFAULT_HASH;
	uint32_t LastRemoteHashReceived = ILOLA_INVALID_MILLIS;

	uint32_t StateStartTime = ILOLA_INVALID_MILLIS;
	uint32_t SubStateStart = ILOLA_INVALID_MILLIS;

protected:
	enum SyncStateEnum : uint8_t
	{
		Starting = 0,
		WaitingForTrigger = 1,
		FullSync = 2,
		Synced = 3,
		Resync = 4,
		Disabled = 5
	};

	boolean LocalHashNeedsUpdate = false;

	ITrackedSurfaceNotify * TrackedSurface = nullptr;

	SyncStateEnum SyncState = SyncStateEnum::Disabled;

	TinyCrc CalculatorCRC;
	LoLaPacketSlim PacketHolder;

protected:
	virtual void OnWaitingForTriggerService() {}
	virtual void OnSyncActive() {}
	virtual void OnSyncedService() {}

	virtual void OnSurfaceDataChanged() {}
	virtual void OnStateUpdated(const SyncStateEnum newState) {}

public:
	AbstractSync(Scheduler* scheduler, const uint16_t period, ILoLa* loLa, ITrackedSurfaceNotify* trackedSurface)
		: IPacketSendService(scheduler, period, loLa, &PacketHolder)
	{
		TrackedSurface = trackedSurface;
		CalculatorCRC.Reset();
		SyncState = SyncStateEnum::Disabled;
	}

	ITrackedSurface* GetSurface()
	{
		return TrackedSurface;
	}

	void OnLinkEstablished()
	{
		Enable();
		SetNextRunASAP();
		UpdateState(SyncStateEnum::Starting);
	}

	void OnLinkLost()
	{
		UpdateState(SyncStateEnum::Disabled);
	}

	bool IsSynced()
	{
		return SyncState == SyncStateEnum::Synced;
	}

	bool IsSyncing()
	{
		return SyncState == SyncStateEnum::Resync || SyncState == SyncStateEnum::FullSync;
	}

	void SurfaceDataChangedEvent(uint8_t param)
	{
		OnSurfaceDataChanged();
	}

	void NotifyDataChanged()
	{
		if (TrackedSurface != nullptr)
		{
			TrackedSurface->NotifyDataChanged();
		}
	}

protected:
	virtual bool OnEnable()
	{
		return true;
	}

	void SetRemoteHash(const uint8_t remoteHash)
	{
		LastRemoteHashReceived = Millis();
		if (LastRemoteHashReceived == ILOLA_INVALID_MILLIS)
		{
			LastRemoteHashReceived--;
		}
		LastRemoteHash = remoteHash;
	}

	bool IsLastRemoteHashFresh()
	{
		if (LastRemoteHashReceived != ILOLA_INVALID_MILLIS)
		{
			return (Millis() - LastRemoteHashReceived < ABSTRACT_SURFACE_SYNC_PERSISTANCE_PERIOD);
		}
		return true;
	}

	bool IsLastRemoteHashStale()
	{
		if (LastRemoteHashReceived != ILOLA_INVALID_MILLIS)
		{
			return (Millis() - LastRemoteHashReceived > ABSTRACT_SURFACE_SYNC_KEEP_ALIVE_MILLIS);
		}
		return true;
	}

	bool HasRemoteHash()
	{
		return LastRemoteHashReceived != ILOLA_INVALID_MILLIS;
	}

	bool HashesMatch()
	{
		if (!HasRemoteHash())
		{
			Serial.println(F("No Remote hash"));
		}
		if (GetLocalHash() != LastRemoteHash)
		{
			Serial.println(F("Hash mismatch"));
		}
		return (HasRemoteHash() &&
			GetLocalHash() == LastRemoteHash);
	}

	void UpdateLocalHash()
	{
		LocalHashNeedsUpdate = false;
		CalculatorCRC.Reset();

		for (uint8_t i = 0; i < TrackedSurface->GetDataSize(); i++)
		{
			CalculatorCRC.Update(TrackedSurface->GetData()[i]);
		}
	}

	uint8_t GetLocalHash()
	{
		return CalculatorCRC.GetCurrent();
	}

	inline void InvalidateLocalHash()
	{
		LocalHashNeedsUpdate = true;
	}

	inline void InvalidateRemoteHash()
	{
		LastRemoteHash = ABSTRACT_SURFACE_DEFAULT_HASH;
		LastRemoteHashReceived = ILOLA_INVALID_MILLIS;
	}

	inline uint32_t SubStateElapsed()
	{
		if (SubStateStart != ILOLA_INVALID_MILLIS)
		{
			return Millis() - SubStateStart;
		}
		else
		{
			return 0;
		}
	}

	void StampSubStateStart(const int16_t offset = 0)
	{
		SubStateStart = Millis() + offset;
	}

	void ResetSubStateStart()
	{
		SubStateStart = ILOLA_INVALID_MILLIS;
	}

	uint32_t GetElapsedSinceStateStart()
	{
		if (StateStartTime != ILOLA_INVALID_MILLIS)
		{
			return Millis() - StateStartTime;
		}
		else
		{
			return ILOLA_INVALID_MILLIS;
		}
	}

	void UpdateState(const SyncStateEnum newState)
	{
		if (SyncState != newState)
		{
			StateStartTime = Millis();

			SetNextRunASAP();

			switch (SyncState)
			{
			case SyncStateEnum::Disabled:
				Enable();
				break;
			case SyncStateEnum::Starting:
			case SyncStateEnum::WaitingForTrigger:
			case SyncStateEnum::FullSync:
			case SyncStateEnum::Synced:
			case SyncStateEnum::Resync:
			default:
				break;
			}

			Serial.print(Millis());
			Serial.print(F(": Updated State to "));
			switch (newState)
			{
			case SyncStateEnum::WaitingForTrigger:
				Serial.println(F("WaitingForTrigger"));
				break;
			case SyncStateEnum::Starting:
				Serial.println(F("Starting"));
				InvalidateLocalHash();
				break;
			case SyncStateEnum::FullSync:
				Serial.println(F("FullSync"));
				InvalidateRemoteHash();
				TrackedSurface->GetTracker()->SetAllPending();
				Enable();
				break;
			case SyncStateEnum::Synced:
				Serial.println(F("Synced"));
				TrackedSurface->GetTracker()->ClearAllPending();
				break;
			case SyncStateEnum::Resync:
				Serial.println(F("Resync"));
				break;
			case SyncStateEnum::Disabled:
				Serial.println(F("Disabled"));
				break;
			default:
				break;
			}

			SyncState = newState;

			OnStateUpdated(SyncState);
		}
	}

	virtual bool OnSetup()
	{
		if (IPacketSendService::OnSetup() && TrackedSurface != nullptr)
		{
			if (TrackedSurface->Setup())
			{
				MethodSlot<AbstractSync, uint8_t> memFunSlot(this, &AbstractSync::SurfaceDataChangedEvent);
				TrackedSurface->AttachOnSurfaceUpdated(memFunSlot);
				return true;
			}
		}

		return false;
	}

	void OnService()
	{
		//Make sure hash is up to date.
		if (LocalHashNeedsUpdate)
		{
			UpdateLocalHash();
		}

		switch (SyncState)
		{
		case SyncStateEnum::Starting:
			SetNextRunASAP();
			UpdateState(SyncStateEnum::WaitingForTrigger);
			break;
		case SyncStateEnum::WaitingForTrigger:
			OnWaitingForTriggerService();
			break;
		case SyncStateEnum::FullSync:
			OnSyncActive();
			break;
		case SyncStateEnum::Synced:
			OnSyncedService();
			break;
		case SyncStateEnum::Resync:
			if (GetElapsedSinceStateStart() > ABSTRACT_SURFACE_MAX_ELAPSED_BEFORE_RESYNC_DEMOTE)
			{
				UpdateState(SyncStateEnum::FullSync);
			}
			else
			{
				OnSyncActive();
			}
			break;
		case SyncStateEnum::Disabled:
		default:
			SyncState = SyncStateEnum::Disabled;
			Disable();
			break;
		}
	}
};
#endif