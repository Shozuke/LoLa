// 
// 
// 

#include <PacketDriver\LoLaPacketDriver.h>

LoLaPacketDriver::LoLaPacketDriver() : ILoLa()
{
}

void LoLaPacketDriver::OnBatteryAlarm()
{
}

void LoLaPacketDriver::OnSentOk()
{
}

void LoLaPacketDriver::OnWakeUpTimer()
{
}

//When RF detects incoming packet.
void LoLaPacketDriver::OnIncoming(const int16_t rssi)
{
	LastReceived = GetMillis();
	LastReceivedRssi = rssi;
	if (!IncomingInfo.HasInfo())
	{
		IncomingInfo.SetInfo(LastReceived, LastReceivedRssi);
	}
}

//When RF has packet to read.
void LoLaPacketDriver::OnReceiveBegin(const uint8_t length, const int16_t rssi)
{
	if (!IncomingInfo.HasInfo())
	{
		IncomingInfo.SetInfo(GetMillis(), rssi);
	}
	Receiver.SetBufferSize(length);
}

//When RF has received a garbled packet.
void LoLaPacketDriver::OnReceivedFail(const int16_t rssi)
{
	IncomingInfo.Clear();
}

void LoLaPacketDriver::OnReceived()
{
	if (!IncomingInfo.HasInfo())
	{
		IncomingInfo.SetInfo(GetMillis(), LastReceivedRssi);
	}

	if (!SetupOk || !Enabled || !Receiver.ReceivePacket() || !(Receiver.GetIncomingDefinition() != nullptr))
	{
	}
	else 
	{	//Packet received Ok, let's update that info really quick.
		LastValidReceived = IncomingInfo.PacketTime;
		LastValidReceivedRssi = IncomingInfo.PacketRSSI;
		IncomingInfo.Clear();
		//Is Ack
		if (Receiver.GetIncomingDefinition()->GetHeader() == PACKET_DEFINITION_ACK_HEADER)
		{
			Services.ProcessAck(Receiver.GetIncomingPacket());
		}
		//Is packet
		else
		{
			if (Receiver.GetIncomingDefinition()->HasACK())
			{
				if (Sender.SendAck(Receiver.GetIncomingDefinition(), Receiver.GetIncomingPacket()->GetId()))
				{
					if (Transmit())
					{
						LastSent = GetMillis();
					}
				}
			}
			//Handle packet.
			Services.ProcessPacket(Receiver.GetIncomingPacket());
		}
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
		(GetMillis() - LastSent > LOLA_PACKET_MANAGER_SEND_MIN_BACK_OFF_DURATION_MILLIS)
		&&
		(GetMillis() - LastValidReceived > LOLA_PACKET_MANAGER_SEND_AFTER_RECEIVE_MIN_BACK_OFF_DURATION_MILLIS)
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
				LastSent = GetMillis();
				return true;
			}
		}
	}

	return false;
}

uint32_t LoLaPacketDriver::GetLastValidReceivedMillis()
{ 
	return  LastValidReceived;
}

int16_t LoLaPacketDriver::GetLastValidRSSI() 
{ 
	return LastValidReceivedRssi;
}