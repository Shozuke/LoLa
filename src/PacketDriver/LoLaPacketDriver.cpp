// 
// 
// 

#include <PacketDriver\LoLaPacketDriver.h>

LoLaPacketDriver::LoLaPacketDriver(Scheduler* scheduler) : ILoLa(), EventQueue(scheduler)
{
	MethodSlot<LoLaPacketDriver, uint8_t> DriverActionSlot(this, &LoLaPacketDriver::OnAsyncEvent);
	EventQueue.AttachActionCallback(DriverActionSlot);
}

void LoLaPacketDriver::OnBatteryAlarm()
{
	EventQueue.AppendEventToQueue(AsyncActionsEnum::FireBatteryAlarm);
}

void LoLaPacketDriver::OnSentOk()
{
	//TODO: Log for statistics.
}

void LoLaPacketDriver::OnWakeUpTimer()
{
	EventQueue.AppendEventToQueue(AsyncActionsEnum::FireWakeUpTimer);
}

//When RF detects incoming packet.
void LoLaPacketDriver::OnIncoming(const int16_t rssi)
{
	LastReceived = GetMillis();
	LastReceivedRssi = rssi;
	IncomingInfo.SetInfo(LastReceived, LastReceivedRssi);
}

//When RF has packet to read.
void LoLaPacketDriver::OnReceiveBegin(const uint8_t length, const int16_t rssi)
{
	if (!IncomingInfo.HasInfo())
	{
		IncomingInfo.SetInfo(GetMillis(), rssi);
	}
	Receiver.SetBufferSize(length);

	//Asynchronously process the received packet.
	EventQueue.AppendEventToQueue(AsyncActionsEnum::ActionReceivePacket);
}

//When RF has received a garbled packet.
void LoLaPacketDriver::OnReceivedFail(const int16_t rssi)
{
	IncomingInfo.Clear();
}

void LoLaPacketDriver::CheckPendingAsync()
{
	//Asynchronously check for pending messages from the radio IC.
	EventQueue.AppendEventToQueue(AsyncActionsEnum::ActionCheckPending);
}

void LoLaPacketDriver::ReceivePacket()
{
	if (!IncomingInfo.HasInfo())
	{
		IncomingInfo.SetInfo(GetMillis(), LastReceivedRssi);
	}

	if (!SetupOk || !Enabled || !Receiver.ReceivePacket() || !(Receiver.GetIncomingDefinition() != nullptr))
	{
	}
	else
	{	//Packet received Ok, let's commit that info really quick.
		LastValidReceived = IncomingInfo.GetPacketTime();
		LastValidReceivedRssi = IncomingInfo.GetPacketRSSI();
		IncomingInfo.Clear();
		//Is Ack.
		if (Receiver.GetIncomingDefinition()->GetHeader() == PACKET_DEFINITION_ACK_HEADER)
		{
			Services.ProcessAck(Receiver.GetIncomingPacket());
		}
		//Is packet.
		else
		{
			if (Receiver.GetIncomingDefinition()->HasACK())
			{
				if (!Sender.IsClear())
				{
					//TODO: Missed outbound packet, store statistics.
					Sender.Clear();
				}

				if (Sender.SendAck(Receiver.GetIncomingDefinition()->GetHeader(), Receiver.GetIncomingPacket()->GetId()))
				{
					SenderTransmit();
					//TODO: Send Ack in return send slot when we have link.
				}
				else
				{
					//TODO: Unable to send packet, log error.
#ifdef DEBUG_LOLA
					Serial.println("Failed to send ack packet.");
#endif // DEBUG_LOLA
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

bool LoLaPacketDriver::HotAfterSend()
{
	if (LastSent != ILOLA_INVALID_MILLIS)
	{
		return GetMillis() - LastSent < LOLA_PACKET_MANAGER_SEND_MIN_BACK_OFF_DURATION_MILLIS;
	}
	return false;
}

bool LoLaPacketDriver::HotAfterReceive()
{
	if (LastValidReceived != ILOLA_INVALID_MILLIS)
	{
		return GetMillis() - LastValidReceived < LOLA_PACKET_MANAGER_SEND_AFTER_RECEIVE_MIN_BACK_OFF_DURATION_MILLIS;
	}
	return false;
}

void LoLaPacketDriver::WakeUpTimerFired()
{
	//TODO: What to do with this event?
}

void LoLaPacketDriver::BatteryAlarmFired()
{
	//TODO: Set up callback for health report?
}

void LoLaPacketDriver::SenderTransmit()
{
	if (!Sender.IsClear())
	{
		//Nothing to transmit.
		return;
	}

	if (Transmit())
	{
		LastSent = GetMillis();
		return;
	}
	else
	{
		//Transmit failed.
		//TODO: Store statistics.
	}
}

bool LoLaPacketDriver::IsInSendSlot()
{
	SendSlotElapsed = GetMillisSync() % DuplexPeriodMillis;

	//Even spread of true and false across the DuplexPeriod
	if (EvenSlot)
	{
		if (SendSlotElapsed < DuplexPeriodMillis / 2)
		{
			return true;
		}
	}
	else
	{
		if (SendSlotElapsed >= DuplexPeriodMillis / 2)
		{
			return true;
		}
	}

	return false;
}

bool LoLaPacketDriver::AllowedSend(const bool overridePermission)
{
	if (!Enabled)
	{
		return false;
	}

#ifdef USE_TIME_SLOT
	if (IsLinkActive())
	{
		return CanTransmit() &&
			(overridePermission || IsInSendSlot());
	}
	else
	{
		return CanTransmit() && overridePermission &&
			!HotAfterSend() && !HotAfterReceive();
	}

#else
	return Enabled &&
		!HotAfterSend() && !HotAfterReceive() &&
		CanTransmit() &&
		(overridePermission || IsLinkActive());
#endif
}

void LoLaPacketDriver::SetCryptoSeedSource(ISeedSource* cryptoSeedSource)
{
	Sender.SetCryptoSeedSource(cryptoSeedSource);
	Receiver.SetCryptoSeedSource(cryptoSeedSource);
}

//TODO:Store statistics metadata
bool LoLaPacketDriver::SendPacket(ILoLaPacket* packet)
{
	if (SetupOk)
	{
		if (Sender.SendPacket(packet))
		{
			SenderTransmit();
			return true;
		}
	}

	return false;
}

void LoLaPacketDriver::OnAsyncEvent(const uint8_t actionCode)
{
	switch ((AsyncActionsEnum)actionCode)
	{
	case AsyncActionsEnum::ActionReceivePacket:
		ReceivePacket();
		break;
	case AsyncActionsEnum::ActionCheckPending:
		CheckPending();
		break;
	case AsyncActionsEnum::FireBatteryAlarm:
		BatteryAlarmFired();
		break;
	case AsyncActionsEnum::FireWakeUpTimer:
		WakeUpTimerFired();
		break;
	case AsyncActionsEnum::ActionUpdateChannel:
		OnChannelUpdated();
		break;
	case AsyncActionsEnum::ActionUpdateTransmitPower:
		OnTransmitPowerUpdated();
		break;
	case AsyncActionsEnum::ActionTerminate:
		OnStop();
		break;
	default:
		break;
	}
}