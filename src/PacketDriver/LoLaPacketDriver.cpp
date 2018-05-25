// 
// 
// 

#include <PacketDriver\LoLaPacketDriver.h>

LoLaPacketDriver::LoLaPacketDriver() : ILoLa()
{
}

void LoLaPacketDriver::OnBatteryAlarm()
{
//#ifdef DEBUG_LOLA
//	Serial.println(F("Low battery."));
//#endif // DEBUG_LOLA
}

void LoLaPacketDriver::OnSentOk()
{
}

void LoLaPacketDriver::OnWakeUpTimer()
{
//#ifdef DEBUG_LOLA
//	Serial.println(F("OnWakeUpTimer"));
//#endif // DEBUG_LOLA
}

//When RF detects incoming packet.
void LoLaPacketDriver::OnIncoming(const int16_t rssi)
{
	LastReceived = millis();
	LastReceivedRssi = rssi;
}

//When RF has packet to read.
void LoLaPacketDriver::OnReceiveBegin(const uint8_t length, const  int16_t rssi)
{
#ifdef DEBUG_LOLA
	Serial.println('-');
#endif // DEBUG_LOLA
	Receiver.SetBufferSize(length);	
}

//When RF has received a garbled packet.
void LoLaPacketDriver::OnReceivedFail(const int16_t rssi)
{
#ifdef DEBUG_LOLA
	Serial.println('!');
#endif // DEBUG_LOLA
	LastReceivedRssi = rssi;
}

void LoLaPacketDriver::OnReceived()
{	
	if (!SetupOk || !Enabled || !Receiver.ReceivePacket() || !(Receiver.GetIncomingDefinition() != nullptr))
	{
		return;
	}

	//Is Ack
	if (Receiver.GetIncomingDefinition()->GetHeader() == PACKET_DEFINITION_ACK_HEADER)
	{
		Services.ProcessAck(Receiver.GetIncomingPacket());
	}
	else//Is packet
	{
		if (Receiver.GetIncomingDefinition()->HasACK())
		{
			if (Sender.SendAck(Receiver.GetIncomingDefinition(), Receiver.GetIncomingPacket()->GetId()))
			{
				if (Transmit())
				{
					LastSent = millis();
				}
			}
		}
		//Handle packet.
		Services.ProcessPacket(Receiver.GetIncomingPacket());
	}
}

bool LoLaPacketDriver::Setup()
{
	SetupOk = false;
	if (Receiver.Setup(&PacketMap) &&
		Sender.Setup(&PacketMap))
	{
		SetupOk = true;
	}

	return SetupOk;
}

LoLaServicesManager* LoLaPacketDriver::GetServices()
{
	return &Services;
}

bool LoLaPacketDriver::AllowedSend()
{
	return Enabled &&
		SendPermission &&
		(millis() - LastSent > LOLA_PACKET_MANAGER_SEND_MIN_BACK_OFF_DURATION_MILLIS)
		&&
		(millis() - LastReceived > LOLA_PACKET_MANAGER_SEND_AFTER_RECEIVE_MIN_BACK_OFF_DURATION_MILLIS)
		&&
		CanTransmit();
}

//TODO:Store statistics metadata
bool LoLaPacketDriver::SendPacket(ILoLaPacket* packet)
{
	if (SetupOk)
	{
		if (Sender.SendPacket(packet))
		{
			if (Transmit())
			{
				LastSent = millis();
				return true;
			}
		}
	}

	return false;
}

