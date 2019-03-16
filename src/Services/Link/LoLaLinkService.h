// LoLaLinkService.h

#ifndef _LOLA_LINK_SERVICE_h
#define _LOLA_LINK_SERVICE_h


#include <Services\Link\AbstractLinkService.h>


#include <Services\Link\LoLaLinkClockSyncer.h>

#include <LoLaCrypto\LoLaCryptoKeyExchange.h>
#include <LoLaCrypto\LoLaCryptoEncoder.h>

#include <Services\Link\LoLaLinkPowerBalancer.h>
#include <Services\Link\LoLaLinkChannelManager.h>

#include <Services\Link\LoLaLinkTimedHopper.h>

class LoLaLinkService : public AbstractLinkService
{
#ifdef DEBUG_LOLA
protected:
	uint32_t LinkingDuration = 0;
	uint32_t PKCDuration = 0;

private:
	uint32_t NextDebug = 0;
#define LOLA_LINK_DEBUG_UPDATE_MILLIS					((uint32_t)1000*(uint32_t)LOLA_LINK_DEBUG_UPDATE_SECONDS)

#else
private:
#endif

	//Sub-services.
	LoLaLinkTimedHopper TimedHopper;

	//Channel management.
	LoLaLinkChannelManager ChannelManager;

	//Power balancer.
	LoLaLinkPowerBalancer PowerBalancer;

	//Crypto Token.
	ITokenSource* CryptoToken = nullptr;

	//Link report tracking.
	bool ReportPending = false;

protected:
	//Crypto key exchanger.
	LoLaCryptoKeyExchanger	KeyExchanger;
	uint32_t KeysLastGenerated = ILOLA_INVALID_MILLIS;
	//

	//Synced Clock.
	LoLaLinkClockSyncer* ClockSyncerPointer = nullptr;
	IClockSyncTransaction* ClockSyncTransaction = nullptr;

	//Shared Sub state helpers.
	uint32_t SubStateStart = ILOLA_INVALID_MILLIS;
	uint8_t LinkingState = 0;
	uint8_t InfoSyncStage = 0;

protected:
	///Host packet handling.
	//Unlinked packets.
	virtual void OnLinkDiscoveryReceived() {}
	virtual void OnPKCRequestReceived(const uint8_t sessionId, const uint32_t remoteMACHash) {}
	virtual void OnRemotePublicKeyReceived(const uint8_t sessionId, uint8_t * encodedPublicKey) {}

	//Linked packets.
	virtual void OnRemoteInfoSyncReceived(const uint8_t rssi) {}
	virtual void OnHostInfoSyncRequestReceived() {}
	virtual void OnClockSyncRequestReceived(const uint8_t requestId, const uint32_t estimatedMillis) {}
	virtual void OnClockSyncTuneRequestReceived(const uint8_t requestId, const uint32_t estimatedMillis) {}
	///

	///Remote packet handling.
	//Unlinked packets.
	virtual void OnIdBroadcastReceived(const uint8_t sessionId, const uint32_t hostMACHash) {}
	virtual void OnHostPublicKeyReceived(const uint8_t sessionId, uint8_t* hostPublicKey) {}

	//Linked packets.
	virtual void OnHostInfoSyncReceived(const uint8_t rssi, const uint16_t rtt) {}
	virtual void OnClockSyncResponseReceived(const uint8_t requestId, const int32_t estimatedError) {}
	virtual void OnClockSyncTuneResponseReceived(const uint8_t requestId, const int32_t estimatedError) {}
	///

	//Internal housekeeping.
	virtual void OnClearSession() {};
	virtual void OnLinkStateChanged(const LoLaLinkInfo::LinkStateEnum newState) {}

	//Runtime handlers.
	virtual void OnLinking() { SetNextRunDelay(LOLA_LINK_SERVICE_CHECK_PERIOD); }
	virtual bool OnAwaitingLink() { return false; }
	virtual void OnKeepingLink() { SetNextRunDelay(LOLA_LINK_SERVICE_IDLE_PERIOD); }


public:
	LoLaLinkService(Scheduler* scheduler, ILoLaDriver* driver)
		: AbstractLinkService(scheduler, driver)
		, TimedHopper(scheduler, driver)
#ifdef LOLA_LINK_DIAGNOSTICS_ENABLED
		, Diagnostics(scheduler, loLa, &LinkInfo)
#endif
	{
	}

	bool OnEnable()
	{
		UpdateLinkState(LoLaLinkInfo::LinkStateEnum::Setup);

		return true;
	}

	void OnDisable()
	{
		UpdateLinkState(LoLaLinkInfo::LinkStateEnum::Disabled);
	}

protected:
	bool OnAddSubServices()
	{
		return ServicesManager->Add(&TimedHopper);
	}

	bool OnSetup()
	{
		if (IPacketSendService::OnSetup() &&
			ClockSyncTransaction != nullptr &&
			ClockSyncerPointer != nullptr &&
			ServicesManager != nullptr &&
			KeyExchanger.Setup() &&
			ClockSyncerPointer->Setup(LoLaDriver->GetClockSource()) &&
			ChannelManager.Setup(LoLaDriver) &&
			TimedHopper.Setup(LoLaDriver->GetClockSource(), &ChannelManager))
		{
			LinkInfo = ServicesManager->GetLinkInfo();
			CryptoToken = TimedHopper.GetTokenSource();

			if (LinkInfo != nullptr && CryptoToken != nullptr)
			{
				LinkInfo->SetDriver(LoLaDriver);
				LinkInfo->Reset();
				KeysLastGenerated = ILOLA_INVALID_MILLIS;

				//Make sure to lazy load the local MAC on startup.
				LinkInfo->GetLocalId();

				if (PowerBalancer.Setup(LoLaDriver, LinkInfo))
				{
					ClearSession();
#ifdef DEBUG_LOLA
					Serial.print(F("Local MAC: "));
					LinkInfo->PrintMac(&Serial);
					Serial.println();
					Serial.print(F("\tId: "));
					Serial.println(LinkInfo->GetLocalId());
#endif
					ResetStateStartTime();
					SetNextRunASAP();

					return true;
				}
			}

		}

		return false;
	}

	void OnSendOk(const uint8_t header, const uint32_t sendDuration)
	{
		LastSent = millis();

		if (header == LOLA_LINK_HEADER_REPORT &&
			LinkInfo->HasLink() &&
			ReportPending)
		{
			ReportPending = false;
			LinkInfo->StampLocalInfoLastUpdatedRemotely();
		}

		SetNextRunASAP();
	}

	void OnLinkInfoReportReceived(const uint8_t rssi, const uint32_t partnerReceivedCount)
	{
		if (LinkInfo->HasLink())
		{
			LinkInfo->StampPartnerInfoUpdated();
			LinkInfo->SetPartnerRSSINormalized(rssi);
			LinkInfo->SetPartnerReceivedCount(partnerReceivedCount);

			SetNextRunASAP();
		}
	}

	void SetLinkingState(const uint8_t linkingState)
	{
		LinkingState = linkingState;
		ResetLastSentTimeStamp();

		SetNextRunASAP();
	}

	void OnService()
	{
		switch (LinkInfo->GetLinkState())
		{
		case LoLaLinkInfo::LinkStateEnum::Setup:
			UpdateLinkState(LoLaLinkInfo::LinkStateEnum::AwaitingLink);
			break;
		case LoLaLinkInfo::LinkStateEnum::AwaitingLink:
			if (KeysLastGenerated == ILOLA_INVALID_MILLIS || millis() - KeysLastGenerated > LOLA_LINK_SERVICE_UNLINK_KEY_PAIR_LIFETIME)
			{
#ifdef DEBUG_LOLA_LINK_CRYPTO
				uint32_t SharedKeyTime = micros();
#endif
				KeyExchanger.GenerateNewKeyPair();
				KeysLastGenerated = millis();
#ifdef DEBUG_LOLA
#ifdef DEBUG_LOLA_LINK_CRYPTO
				SharedKeyTime = micros() - SharedKeyTime;
				Serial.print(F("Keys generation took "));
				Serial.print(SharedKeyTime);
				Serial.println(F(" us."));
#endif
#endif
				SetNextRunASAP();
			}
			else
			{
				if (!OnAwaitingLink())//Time out is different for host/remote.
				{
					UpdateLinkState(LoLaLinkInfo::LinkStateEnum::AwaitingSleeping);
				}
			}
			break;
		case LoLaLinkInfo::LinkStateEnum::AwaitingSleeping:
			UpdateLinkState(LoLaLinkInfo::LinkStateEnum::AwaitingLink);
			break;
		case LoLaLinkInfo::LinkStateEnum::Linking:
			if (GetElapsedSinceStateStart() > LOLA_LINK_SERVICE_UNLINK_MAX_BEFORE_LINKING_CANCEL)
			{
				UpdateLinkState(LoLaLinkInfo::LinkStateEnum::AwaitingLink);
			}
			else
			{
				OnLinking();
			}
			break;
		case LoLaLinkInfo::LinkStateEnum::Linked:
			if (GetElapsedLastValidReceived() > LOLA_LINK_SERVICE_LINKED_MAX_BEFORE_DISCONNECT)
			{
				UpdateLinkState(LoLaLinkInfo::LinkStateEnum::AwaitingLink);
			}
			else
			{
				OnKeepingLinkCommon();
			}
			break;
		case LoLaLinkInfo::LinkStateEnum::Disabled:
		default:
			Disable();
			return;
		}
	}

private:
	inline void OnKeepingLinkCommon()
	{
		if (PowerBalancer.Update())
		{
			SetNextRunASAP();
		}
		else if (ReportPending) //Priority update, to keep link info up to date.
		{
			PrepareLinkReport(LinkInfo->GetPartnerLastReportElapsed() > LOLA_LINK_SERVICE_LINKED_INFO_STALE_PERIOD);
			RequestSendPacket();
		}
		else if (GetElapsedLastValidReceived() > LOLA_LINK_SERVICE_LINKED_MAX_PANIC)
		{
			PowerBalancer.SetMaxPower();
			PreparePing(); 		//Send Panic Ping!
			RequestSendPacket();
		}
		else if (LinkInfo->GetLocalInfoUpdateRemotelyElapsed() > LOLA_LINK_SERVICE_LINKED_INFO_UPDATE_PERIOD)
		{
			ReportPending = true; //Send link info update, deferred.
			SetNextRunASAP();
		}
		else
		{
			OnKeepingLink();
		}

#ifdef DEBUG_LOLA
		if (LinkInfo->HasLink())
		{
			if (LinkInfo->GetLinkDuration() >= NextDebug)
			{
				DebugLinkStatistics(&Serial);

				NextDebug = (LinkInfo->GetLinkDuration()/1000)*1000 + LOLA_LINK_DEBUG_UPDATE_MILLIS;

				if (LinkInfo->GetLinkDuration() < LOLA_LINK_DEBUG_UPDATE_MILLIS)
				{
					NextDebug -= (LinkInfo->GetLinkDuration() / 1000)*1000;
				}
			}
		}
#endif
	}

protected:
	void ClearSession()
	{
		ReportPending = false;

		if (ClockSyncerPointer != nullptr)
		{
			ClockSyncerPointer->Reset();
		}

		if (ClockSyncTransaction != nullptr)
		{
			ClockSyncTransaction->Reset();
		}

		if (LinkInfo != nullptr)
		{
			LinkInfo->Reset();
		}

		LoLaDriver->GetCryptoEncoder()->Clear();
		CryptoToken->SetSeed(0);

		KeyExchanger.ClearPartner();

		InfoSyncStage = 0;

		OnClearSession();

#ifdef DEBUG_LOLA
		NextDebug = LinkInfo->GetLinkDuration() + 10000;
#endif
	}

	void UpdateLinkState(const LoLaLinkInfo::LinkStateEnum newState)
	{
		if (LinkInfo->GetLinkState() != newState)
		{
#ifdef DEBUG_LOLA
			if (newState == LoLaLinkInfo::LinkStateEnum::Linked)
			{
				LinkingDuration = GetElapsedSinceStateStart();
			}
#endif
			ResetStateStartTime();
			ResetLastSentTimeStamp();

			//Previous state.
			if (LinkInfo->GetLinkState() == LoLaLinkInfo::LinkStateEnum::Linked)
			{
#ifdef DEBUG_LOLA
				DebugLinkStatistics(&Serial);
#endif
				//Notify all link dependent services to stop.
				ClearSession();
				PowerBalancer.SetMaxPower();
				ChannelManager.ResetChannel();
				LoLaDriver->OnStart();
				ServicesManager->NotifyServicesLinkUpdated(false);
			}

			switch (newState)
			{
			case LoLaLinkInfo::LinkStateEnum::Setup:
				LoLaDriver->Enable();
				SetLinkingState(0);
				ClearSession();
				PowerBalancer.SetMaxPower();
				ChannelManager.ResetChannel();
				LoLaDriver->OnStart();
				SetNextRunASAP();
				break;
			case LoLaLinkInfo::LinkStateEnum::AwaitingLink:
				ClearSession();
				SetLinkingState(0);
				PowerBalancer.SetMaxPower();
				ChannelManager.ResetChannel();
				SetNextRunASAP();
				break;
			case LoLaLinkInfo::LinkStateEnum::AwaitingSleeping:
				ClearSession();
				SetLinkingState(0);
				PowerBalancer.SetMaxPower();
				ChannelManager.ResetChannel();
				LoLaDriver->OnStart();
				//Sleep time is set on Host/Remote virtual.
				break;
			case LoLaLinkInfo::LinkStateEnum::Linking:
				SetLinkingState(0);
				CryptoToken->SetSeed(LoLaDriver->GetCryptoEncoder()->GetSeed());
#ifdef LOLA_LINK_USE_ENCRYPTION
				LoLaDriver->GetCryptoEncoder()->SetEnabled();
#endif
				PowerBalancer.SetMaxPower();
#ifdef DEBUG_LOLA				
				Serial.print(F("Linking to Id: "));
				Serial.println(LinkInfo->GetPartnerId());
				Serial.print(F("\tSession: "));
				Serial.println(LinkInfo->GetSessionId());
				Serial.print(F("PKC took "));
				Serial.print(PKCDuration);
				Serial.println(F(" ms."));
#endif
				SetNextRunASAP();
				break;
			case LoLaLinkInfo::LinkStateEnum::Linked:
				LinkInfo->StampLinkStarted();
				ClockSyncerPointer->StampSynced();
				LoLaDriver->ResetStatistics();
				PowerBalancer.SetMaxPower();
#ifdef DEBUG_LOLA
				Serial.println();
				DebugLinkEstablished();
#endif

				//Notify all link dependent services they can start.
				ServicesManager->NotifyServicesLinkUpdated(true);
				SetNextRunASAP();
				break;
			case LoLaLinkInfo::LinkStateEnum::Disabled:
				LinkInfo->Reset();
			default:
				break;
			}

			OnLinkStateChanged(newState);
			LinkInfo->UpdateState(newState);
		}
	}

	bool ProcessPacket(ILoLaPacket* receivedPacket)
	{
		//Switch the packet to the appropriate method.
		switch (receivedPacket->GetDataHeader())
		{
		case LOLA_LINK_HEADER_SHORT:
			switch (receivedPacket->GetPayload()[0])
			{
				///Unlinked packets.
				//To Host.
			case LOLA_LINK_SUBHEADER_LINK_DISCOVERY:
				OnLinkDiscoveryReceived();
				break;

			case LOLA_LINK_SUBHEADER_REMOTE_PKC_START_REQUEST:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnPKCRequestReceived(receivedPacket->GetId(), ATUI_R.uint);
				break;

				//To remote.
			case LOLA_LINK_SUBHEADER_HOST_ID_BROADCAST:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnIdBroadcastReceived(receivedPacket->GetId(), ATUI_R.uint);
				break;
				///

				///Linking Packets
			case LOLA_LINK_SUBHEADER_NTP_REQUEST:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnClockSyncRequestReceived(receivedPacket->GetId(), ATUI_R.uint);
				break;

				//To Remote.
			case LOLA_LINK_SUBHEADER_NTP_REPLY:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnClockSyncResponseReceived(receivedPacket->GetId(), ATUI_R.iint);
				break;
				///

				///Linked packets.
				//Host.
			case LOLA_LINK_SUBHEADER_NTP_TUNE_REQUEST:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnClockSyncTuneRequestReceived(receivedPacket->GetId(), ATUI_R.uint);
				break;

				//Remote.
			case LOLA_LINK_SUBHEADER_NTP_TUNE_REPLY:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnClockSyncTuneResponseReceived(receivedPacket->GetId(), ATUI_R.iint);
				break;
				///
			default:
				break;
			}
			return true;
		case LOLA_LINK_HEADER_LONG:
			switch (receivedPacket->GetPayload()[0])
			{
				//To Host.
			case LOLA_LINK_SUBHEADER_REMOTE_PUBLIC_KEY:
				OnRemotePublicKeyReceived(receivedPacket->GetId(), &receivedPacket->GetPayload()[1]);
				break;

				//To remote.
			case LOLA_LINK_SUBHEADER_HOST_PUBLIC_KEY:
				OnHostPublicKeyReceived(receivedPacket->GetId(), &receivedPacket->GetPayload()[1]);
				break;
			}
			return true;
		case LOLA_LINK_HEADER_REPORT:
			switch (receivedPacket->GetId())
			{
				//To both.
			case LOLA_LINK_SUBHEADER_LINK_REPORT_WITH_REPLY:
				ReportPending = true;
			case LOLA_LINK_SUBHEADER_LINK_REPORT:
				ArrayToR_Array(&receivedPacket->GetPayload()[1]);
				OnLinkInfoReportReceived(receivedPacket->GetPayload()[0], ATUI_R.uint);
				break;

				//To Host.
			case LOLA_LINK_SUBHEADER_INFO_SYNC_HOST:
				ATUI_R.uint = receivedPacket->GetPayload()[1];
				ATUI_R.uint += receivedPacket->GetPayload()[2] << 8;
				OnHostInfoSyncReceived(receivedPacket->GetPayload()[0], (uint16_t)ATUI_R.uint);
				break;

				//To Remote.
			case LOLA_LINK_SUBHEADER_INFO_SYNC_REMOTE:
				OnRemoteInfoSyncReceived(receivedPacket->GetPayload()[0]);
				break;
			case LOLA_LINK_SUBHEADER_INFO_SYNC_REQUEST:
				OnHostInfoSyncRequestReceived();
				break;
			default:
				break;
			}
			return true;
		default:
			break;
		}

		return false;
	}


protected:
	void PreparePublicKeyPacket(const uint8_t subHeader)
	{
		PrepareLongPacket(LinkInfo->GetSessionId(), subHeader);

		if (!KeyExchanger.GetPublicKeyCompressed(&OutPacket.GetPayload()[1]))
		{
#ifdef DEBUG_LOLA
			Serial.println(F("Unable to read PK"));
#endif
		}
	}

	/////Linking time packets.
	void PrepareLinkReport(const bool requestReply)
	{
		if (requestReply)
		{
			PrepareReportPacket(LOLA_LINK_SUBHEADER_LINK_REPORT_WITH_REPLY);
		}
		else
		{
			PrepareReportPacket(LOLA_LINK_SUBHEADER_LINK_REPORT);
		}

		OutPacket.GetPayload()[0] = LinkInfo->GetRSSINormalized();
		ATUI_S.uint = LoLaDriver->GetReceivedCount();
		S_ArrayToPayload();
	}


#ifdef DEBUG_LOLA
private:
	void DebugLinkStatistics(Stream* serial)
	{
		serial->println();
		serial->println(F("Link Info"));


		serial->print(F("UpTime: "));

		uint32_t AliveSeconds = (LinkInfo->GetLinkDuration() / 1000L);

		if (AliveSeconds / 86400 > 0)
		{
			serial->print(AliveSeconds / 86400);
			serial->print(F("d "));
			AliveSeconds = AliveSeconds % 86400;
		}

		if (AliveSeconds / 3600 < 10)
			serial->print('0');
		serial->print(AliveSeconds / 3600);
		serial->print(':');
		if ((AliveSeconds % 3600) / 60 < 10)
			serial->print('0');
		serial->print((AliveSeconds % 3600) / 60);
		serial->print(':');
		if ((AliveSeconds % 3600) % 60 < 10)
			serial->print('0');
		serial->println((AliveSeconds % 3600) % 60);

		serial->print(F("Transmit Power: "));
		serial->print((float)(((LinkInfo->GetTransmitPowerNormalized() * 100) / UINT8_MAX)), 0);
		serial->println(F(" %"));

		serial->print(F("RSSI: "));
		serial->print((float)(((LinkInfo->GetRSSINormalized() * 100) / UINT8_MAX)), 0);
		serial->println(F(" %"));
		serial->print(F("RSSI Partner: "));
		serial->print((float)(((LinkInfo->GetPartnerRSSINormalized() * 100) / UINT8_MAX)), 0);
		serial->println(F(" %"));

		serial->print(F("Sent: "));
		serial->println(LoLaDriver->GetSentCount());
		serial->print(F("Partner Got: "));
		serial->println(LinkInfo->GetPartnerReceivedCount());
		serial->print(F("Lost: "));
		serial->println(max(LinkInfo->GetPartnerReceivedCount(), LoLaDriver->GetSentCount()) - LinkInfo->GetPartnerReceivedCount());
		serial->print(F("Received: "));
		serial->println(LoLaDriver->GetReceivedCount());
		serial->print(F("Rejected: "));
		serial->print(LoLaDriver->GetRejectedCount());

		serial->print(F(" ("));
		if (LoLaDriver->GetReceivedCount() > 0)
		{
			serial->print((float)(LoLaDriver->GetRejectedCount() * 100) / (float)LoLaDriver->GetReceivedCount(), 2);
			serial->println(F(" %)"));
		}
		else if (LoLaDriver->GetRejectedCount() > 0)
		{
			serial->println(F("100 %)"));
		}
		else
		{
			serial->println(F("0 %)"));
		}
		serial->print(F("Timming Collision: "));
		serial->println(LoLaDriver->GetTimingCollisionCount());

		serial->print(F("ClockSync adjustments: "));
		serial->println(LinkInfo->GetClockSyncAdjustments());
		serial->println();
	}

	void DebugLinkEstablished()
	{
#ifdef LOLA_LINK_USE_ENCRYPTION
		Serial.print(F("Link secured with 160 bit "));
		KeyExchanger.Debug(&Serial);
		Serial.println();
		Serial.print(F("\tEncrypted with 128 bit cypher "));
		LoLaDriver->GetCryptoEncoder()->Debug(&Serial);
		Serial.println();
		Serial.print(F("\tProtected with 32 bit TOTP @ "));
		Serial.print(LOLA_LINK_SERVICE_LINKED_TIMED_HOP_PERIOD_MILLIS);
		Serial.println(F(" ms"));
#else
		Serial.print(F("Linked: "));
		Serial.println(MillisSync());
#endif
		Serial.print(F("Linking took "));
		Serial.print(LinkingDuration);
		Serial.println(F(" ms."));
		Serial.print(F("Round Trip Time: "));
		Serial.print(LinkInfo->GetRTT());
		Serial.println(F(" us"));
		Serial.print(F("Estimated Latency: "));
		Serial.print((float)LinkInfo->GetRTT() / (float)2000, 2);
		Serial.println(F(" ms"));
		Serial.print(F("Latency compensation: "));
		Serial.print(LoLaDriver->GetETTM());
		Serial.println(F(" ms"));
	}
#endif

};
#endif