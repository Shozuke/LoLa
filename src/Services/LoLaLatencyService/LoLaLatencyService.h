// LoLaLatencyService.h

#ifndef _LOLA_LATENCY_SERVICE_h
#define _LOLA_LATENCY_SERVICE_h

#include <Services\IPacketSendService.h>
#include <Packet\LoLaPacket.h>
#include <Packet\LoLaPacketMap.h>
#include <RingBufCPP.h>
#include <Callback.h>


#define LOLA_LATENCY_PING_DATA_POINT_STACK_SIZE						3
#define LOLA_LATENCY_PING_DATA_MAX_DEVIATION_SIGMA					((float)0.2)

//65536 is the max uint16_t, about 65 ms max latency is accepted.
#define LOLA_LATENCY_SERVICE_PING_TIMEOUT_MICROS					65000

#define LOLA_LATENCY_SERVICE_POLL_PERIOD_MILLIS						50
#define LOLA_LATENCY_SERVICE_BACK_OFF_DURATION_MILLIS				1000

#define LOLA_LATENCY_SERVICE_SEND_BACK_OFF_DURATION_MILLIS			120

#define LOLA_LATENCY_SERVICE_NO_FULL_RESPONSE_RETRY_DURATION_MILLIS	100
#define LOLA_LATENCY_SERVICE_NO_REPLY_TIMEOUT_MILLIS				(1000 + LOLA_LATENCY_PING_DATA_POINT_STACK_SIZE*(LOLA_SEND_SERVICE_REPLY_TIMEOUT_MILLIS+LOLA_LATENCY_SERVICE_SEND_BACK_OFF_DURATION_MILLIS))
#define LOLA_LATENCY_SERVICE_UNABLE_TO_COMMUNICATE_TIMEOUT_MILLIS	3000

#define PACKET_DEFINITION_PING_HEADER								(PACKET_DEFINITION_CONNECTION_HEADER+1)
#define PACKET_DEFINITION_PING_PAYLOAD_SIZE							0

class LoLaLatencyService : public IPacketSendService
{
private:
	class PingPacketDefinition : public PacketDefinition
	{
	public:
		uint8_t GetConfiguration() { return PACKET_DEFINITION_MASK_HAS_ACK | PACKET_DEFINITION_MASK_HAS_ID; }
		uint8_t GetHeader() { return PACKET_DEFINITION_PING_HEADER; }
		uint8_t GetPayloadSize() { return PACKET_DEFINITION_PING_PAYLOAD_SIZE; }
	} PingDefinition;

	enum LatencyServiceStateEnum
	{
		Setup,
		Starting,
		Checking,
		Sending,
		WaitingForAck,
		BackOff,
		ShortTimeOut,
		LongTimeOut,
		AnalysingResults,
		Done
	} State = LatencyServiceStateEnum::Done;

	LoLaPacketNoPayload PacketHolder;//Optimized memory usage grunt packet.

	uint32_t LastStartedMillis = ILOLA_INVALID_MILLIS;
	uint32_t LastSentTimeStamp = ILOLA_INVALID_MILLIS;
	volatile uint8_t SentId;

	RingBufCPP<uint16_t , PROCESS_EVENT_QUEUE_MAX_QUEUE_DEPTH> DurationStack;

	uint16_t SampleDuration = ILOLA_INVALID_LATENCY;
	uint32_t DurationSum = ILOLA_INVALID_MILLIS;

	//Callback handler
	Signal<const bool> MeasurementCompleteEvent;

public:
	LoLaLatencyService(Scheduler* scheduler, ILoLa* loLa)
		: IPacketSendService(scheduler, LOLA_LATENCY_SERVICE_POLL_PERIOD_MILLIS, loLa, &PacketHolder)
	{
	}

	float GetLatency()
	{
		return ((float)GetRTT() / (float)2000);
	}

	uint16_t GetRTT()
	{
		return GetAverage();
	}

	void RequestRefreshPing()
	{
		Enable();
		State = LatencyServiceStateEnum::Setup;
	}

	void RequestSinglePing()
	{
		PreparePacket();
		RequestSendPacket(LOLA_LATENCY_SERVICE_NO_FULL_RESPONSE_RETRY_DURATION_MILLIS);
		State = LatencyServiceStateEnum::Done;
		Enable();
	}

	void SetMeasurementCompleteCallback(const Slot<const bool>& slot)
	{
		MeasurementCompleteEvent.attach(slot);
	}
	
private:
	uint16_t GetAverage()
	{
		if (DurationStack.numElements() > LOLA_LATENCY_PING_DATA_POINT_STACK_SIZE - 1)
		{
			DurationSum = 0;
			for (uint8_t i = 0; i < DurationStack.numElements(); i++)
			{
				DurationSum += *DurationStack.peek(i);
			}

			//Average is temporarily stored in DurationSum.
			DurationSum = DurationSum / DurationStack.numElements();

			uint16_t MaxDeviation = ceil((float)DurationSum)*(LOLA_LATENCY_PING_DATA_MAX_DEVIATION_SIGMA);
			for (uint8_t i = 0; i < DurationStack.numElements(); i++)
			{
				if (abs((int32_t)*DurationStack.peek(i) - DurationSum) > MaxDeviation)
				{
					return ILOLA_INVALID_LATENCY;
				}
			}

			//Value is always smaller than uint16, because samples with higher value are filtered out on acquisition.
			return (uint16_t)DurationSum;			
		}
		else
		{
			return ILOLA_INVALID_LATENCY;
		}
	}

	void ClearDurations()
	{
		while (!DurationStack.isEmpty())
		{
			DurationStack.pull();
		}
		LastSentTimeStamp = 0;
	}

	void PreparePacket()
	{
		SentId = random(0xFF);
		PacketHolder.SetDefinition(&PingDefinition);
		PacketHolder.SetId(SentId);
	}

protected:
#ifdef DEBUG_LOLA
	void PrintName(Stream* serial)
	{
		serial->print(F("Latency Diagnostics"));
	}
#endif // DEBUG_LOLA

	bool OnEnable()
	{
		return true;
	}

	void OnDisable()
	{
	}

	bool OnAddPacketMap(LoLaPacketMap* packetMap)
	{
		return packetMap->AddMapping(&PingDefinition);
	}

	bool ShouldRespondToOutsidePackets()
	{
		return IsSetupOk() && (State != LatencyServiceStateEnum::Done);
	}

	void SetNextRunDelayRandom(const uint16_t range)
	{
		SetNextRunDelay(range / 2 + random(range / 2));
	}

	bool ShouldWakeUpOnOutsidePacket()
	{
		if (IsSetupOk() &&
			((State == LatencyServiceStateEnum::Done) || (State == LatencyServiceStateEnum::ShortTimeOut)))
		{
			SetNextRunDelayRandom(LOLA_LATENCY_SERVICE_NO_FULL_RESPONSE_RETRY_DURATION_MILLIS);
			return true;
		}

		return false;
	}

	bool ProcessPacket(ILoLaPacket* incomingPacket, const uint8_t header)
	{
		if (header == PACKET_DEFINITION_PING_HEADER)
		{
			ShouldWakeUpOnOutsidePacket();
			return true;
		}
		return false;
	}

	bool ProcessAck(const uint8_t header, const uint8_t id)
	{
		SampleDuration = Micros() - LastSentTimeStamp;

		if (header == PACKET_DEFINITION_PING_HEADER)
		{
			if (!ShouldWakeUpOnOutsidePacket() && ShouldRespondToOutsidePackets())
			{
				if (State == LatencyServiceStateEnum::Sending || State == LatencyServiceStateEnum::WaitingForAck)
				{
					if (LastSentTimeStamp != ILOLA_INVALID_MILLIS && SentId == id &&
						SampleDuration < LOLA_LATENCY_SERVICE_PING_TIMEOUT_MICROS)
					{
						DurationStack.addForce(SampleDuration);
					}
					State = LatencyServiceStateEnum::BackOff;
					SetNextRunASAP();
				}
			}
			return true;
		}
		else
		{
			return false;
		}
	}

	bool OnSetup()
	{
		if (!IPacketSendService::OnSetup())
		{
			return false;
		}

		State = LatencyServiceStateEnum::Done;
		SetNextRunDefault();
		Disable();
		return true;
	}

	void OnSendOk()
	{
		SetNextRunASAP();
	}

	void CancelSample()
	{
		if (State == LatencyServiceStateEnum::Sending)
		{
			ClearSendRequest();
			State = LatencyServiceStateEnum::Checking;
			LastSentTimeStamp = ILOLA_INVALID_MILLIS;
		}		
	}

	void OnSendDelayed()
	{
		CancelSample();
	}

	void OnSendRetrying()
	{
		CancelSample();
	}

	void OnSendFailed()
	{
		CancelSample();		
	}

	void OnSendTimedOut()
	{
		CancelSample();
	}

	void OnService()
	{
		switch (State)
		{
		case LatencyServiceStateEnum::Setup:
#ifdef DEBUG_LOLA
			Serial.println(F("Latency started"));
#endif			
			ClearDurations();
			LastStartedMillis = Millis();
			State = LatencyServiceStateEnum::Starting;
			SetNextRunDefault();
			break;
		case LatencyServiceStateEnum::Starting:
//#ifdef DEBUG_LOLA
//			Serial.println(F("Measuring..."));
//#endif	
			ClearDurations();		
#ifdef MOCK_RADIO
#ifdef DEBUG_LOLA
			Serial.println(F("Mock Latency done."));
#endif
			State = LatencyServiceStateEnum::Done;
			MeasurementCompleteEvent.fire(true);
#else
			State = LatencyServiceStateEnum::Checking;
#endif
			SetNextRunASAP();
			break;
		case LatencyServiceStateEnum::Checking:
			//Have we timed out for good?
			if (Millis() - LastStartedMillis > LOLA_LATENCY_SERVICE_UNABLE_TO_COMMUNICATE_TIMEOUT_MILLIS)
			{
				State = LatencyServiceStateEnum::Done;
#ifdef MOCK_RADIO
				MeasurementCompleteEvent.fire(true);
#else
				MeasurementCompleteEvent.fire(false);
#endif
				
				SetNextRunASAP();
			}
			//Have we timed out for a worst case scenario measurement?
			else if (Millis() - LastStartedMillis > LOLA_LATENCY_SERVICE_NO_REPLY_TIMEOUT_MILLIS)
			{
//#ifdef DEBUG_LOLA
//				Serial.println(F("Latency timed out."));
//#endif
				State = LatencyServiceStateEnum::ShortTimeOut;
				SetNextRunDelayRandom(LOLA_LATENCY_SERVICE_NO_FULL_RESPONSE_RETRY_DURATION_MILLIS);
			}
			//Do we have needed sample count?
			else if (DurationStack.numElements() >= LOLA_LATENCY_PING_DATA_POINT_STACK_SIZE)
			{
				State = LatencyServiceStateEnum::AnalysingResults;
				SetNextRunASAP();
			}
			//All right, lets send a packet and get a sample!
			else
			{
				State = LatencyServiceStateEnum::Sending;
				PreparePacket();
				RequestSendPacket((uint8_t)(LOLA_LATENCY_SERVICE_PING_TIMEOUT_MICROS / 1000));
				LastSentTimeStamp = Micros();
			}
			break;
		case LatencyServiceStateEnum::Sending:
			State = LatencyServiceStateEnum::WaitingForAck;
			SetNextRunDelay(LOLA_SEND_SERVICE_REPLY_TIMEOUT_MILLIS);
			break;
		case LatencyServiceStateEnum::WaitingForAck:
			//If we're here, it means the ack failed to arrive.
			State = LatencyServiceStateEnum::Checking;
			SetNextRunASAP();
			break;
		case LatencyServiceStateEnum::BackOff:
			//If we're here, it means the ack arrived and was valid.
			LastSentTimeStamp = ILOLA_INVALID_MILLIS; //Make sure we ignore stale acks.
			State = LatencyServiceStateEnum::Checking;
			//Do we have needed sample count?
			if (DurationStack.numElements() >= LOLA_LATENCY_PING_DATA_POINT_STACK_SIZE)
			{
				SetNextRunASAP();
			}
			else
			{
				SetNextRunDelayRandom(LOLA_LATENCY_SERVICE_SEND_BACK_OFF_DURATION_MILLIS);
			}
			break;
		case LatencyServiceStateEnum::ShortTimeOut:
			State = LatencyServiceStateEnum::Starting;
			SetNextRunASAP();
			break;
		case LatencyServiceStateEnum::LongTimeOut:
			State = LatencyServiceStateEnum::Starting;
			SetNextRunASAP();
			break;
		case LatencyServiceStateEnum::AnalysingResults:
			if (GetAverage() != ILOLA_INVALID_LATENCY)
			{
#ifdef DEBUG_LOLA
				//Serial.print(F("Latency measurement took: "));
				//Serial.print(Millis() - LastStartedMillis);
				//Serial.println(F(" ms"));
				//Serial.print(F("RTT: "));
				//Serial.print(GetRTT());
				//Serial.println(F(" us"));
				Serial.print(F("Latency: "));
				Serial.print(GetLatency(), 2);
				Serial.println(F(" ms"));
#endif
				State = LatencyServiceStateEnum::Done;
				MeasurementCompleteEvent.fire(true);
				SetNextRunASAP();
			}
			else
			{
				SetNextRunASAP();
				State = LatencyServiceStateEnum::Starting;
			}
			break;
		case LatencyServiceStateEnum::Done:
		default:
			Disable();
			break;
		}
	}
};
#endif