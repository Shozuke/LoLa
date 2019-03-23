// LoLaLinkTimedHopper.h

#ifndef _LOLA_TIMED_HOPPER_h
#define _LOLA_TIMED_HOPPER_h

#include <Services\ILoLaService.h>
#include <Services\Link\LoLaLinkDefinitions.h>

#include <LoLaCrypto\LoLaCryptoTokenSource.h>
#include <Services\Link\LoLaLinkChannelManager.h>



class LoLaLinkTimedHopper : public ILoLaService
{
private:
	//Crypto Token.
	LoLaCryptoTokenSource	CryptoSeed;

	//Synced clock.
	ClockSource* SyncedClock = nullptr;

	//Channel manager.
	LoLaLinkChannelManager* ChannelManager = nullptr;

	//Crypto Encoder.
	LoLaCryptoEncoder* Encoder = nullptr;

	const uint32_t HopPeriod = LOLA_LINK_SERVICE_LINKED_TIMED_HOP_PERIOD_MILLIS;

public:
	LoLaLinkTimedHopper(Scheduler* scheduler, ILoLaDriver* driver)
		: ILoLaService(scheduler, 0, driver)
	{

	}

	bool Setup(ClockSource* syncedClock, LoLaLinkChannelManager* channelManager)
	{
		SyncedClock = syncedClock;
		ChannelManager = channelManager;
		Encoder = LoLaDriver->GetCryptoEncoder();
		if (Encoder != nullptr &&
			ChannelManager != nullptr &&
			CryptoSeed.Setup(SyncedClock))
		{
			CryptoSeed.SetTOTPPeriod(HopPeriod);

			return true;
		}

		return false;
	}

	ITokenSource* GetTokenSource()
	{
		return &CryptoSeed;
	}

protected:
#ifdef DEBUG_LOLA
	void PrintName(Stream* serial)
	{
		serial->print(F("LinkTimedHopper (Cryp"));
		serial->print('|');
#ifdef LOLA_LINK_USE_FREQUENCY_HOP
		serial->print(F("Freq"));
#endif 
		serial->print(')');
	}
#endif // DEBUG_LOLA

	void OnLinkEstablished()
	{
		if (IsSetupOk())
		{
			Enable();
			SetCurrent();
			SetNextRunDelay(CryptoSeed.GetNextSwitchOverMillis());
		}
	}

	void OnLinkLost()
	{
		Encoder->SetToken(0);
		Disable();
	}

	bool Callback()
	{
		SetNextRunDelay(CryptoSeed.GetNextSwitchOverMillis());

		SetCurrent();

		return true;
	}

private:
	void SetCurrent()
	{
		Encoder->SetToken(CryptoSeed.GetToken());

#ifdef LOLA_LINK_USE_FREQUENCY_HOP
		//Use last 8 bits of crypto token to generate a pseudo-random channel distribution.
		ChannelManager->SetNextHop((uint8_t)(Encoder->GetToken() & 0xFF));
#endif
	}

};
#endif