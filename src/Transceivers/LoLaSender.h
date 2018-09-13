// LoLaSender.h

#ifndef _LOLASENDER_h
#define _LOLASENDER_h

#include <Arduino.h>
#include <Transceivers\LoLaBuffer.h>

class LoLaSender : public LoLaBuffer
{
private:
	PacketDefinition* AckDefinition;

	TemplateLoLaPacket<LOLA_PACKET_NO_PAYLOAD_SIZE + 1> AckPacket;

public:
	//Fast Ack, Nack, Ack with Id and Nack with Id packet sender, writes directly to output.
	bool SendPacket(ILoLaPacket* transmitPacket)
	{
		Clear();
		if (transmitPacket != nullptr && transmitPacket->GetDefinition() != nullptr)
		{
			CalculatorCRC.Reset();

			//Crypto starts at the start of the hash.
			CalculatorCRC.Update(GetCryptoSeed());

			//
			CalculatorCRC.Update(transmitPacket->GetDefinition()->GetHeader());

			BufferIndex = LOLA_PACKET_SUB_HEADER_START;
			if (transmitPacket->GetDefinition()->HasId())
			{
				BufferIndex++;
				CalculatorCRC.Update(transmitPacket->GetId());
			}

			for (uint8_t i = 0; i < transmitPacket->GetDefinition()->GetPayloadSize(); i++)
			{
				CalculatorCRC.Update(transmitPacket->GetPayload()[i]);
			}
			transmitPacket->GetRaw()[LOLA_PACKET_MACCRC_INDEX] = CalculatorCRC.GetCurrent();
			BufferSize = transmitPacket->GetDefinition()->GetTotalSize();
			BufferPacket = transmitPacket;
			PacketsProcessed++;
		}

		return !IsClear();
	}

	bool SendAck(const uint8_t header, const uint8_t id)
	{
		Clear();
		CalculatorCRC.Reset();

		//Crypto starts at the start of the hash.
		CalculatorCRC.Update(GetCryptoSeed());
		
		//
		BufferPacket = &AckPacket;
		BufferPacket->SetDefinition(AckDefinition);
		CalculatorCRC.Update(PACKET_DEFINITION_ACK_HEADER);

		//Payload, header and ID
		BufferPacket->GetPayload()[0] = header;
		CalculatorCRC.Update(header);

		BufferPacket->GetPayload()[1] = id;
		CalculatorCRC.Update(id);

		BufferPacket->GetRaw()[LOLA_PACKET_MACCRC_INDEX] = CalculatorCRC.GetCurrent();
		BufferSize = AckDefinition->GetTotalSize();

		PacketsProcessed++;

		return !IsClear();
	}

	bool Setup(LoLaPacketMap* packetMap)
	{
		if (LoLaBuffer::Setup(packetMap))
		{
			AckDefinition = FindPacketDefinition(PACKET_DEFINITION_ACK_HEADER);
			if (AckDefinition != nullptr)
			{
				return true;
			}
		}

		return false;
	}
};

#endif