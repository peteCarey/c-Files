/*//#include "stdafx.h"
#include "CardReader.h"

//#include <SaioReader.h>
#include "Resources.h"
#include "QuantumControl.h"
#include "Display.h"
//#include "SaioInterface.h"*/
#include "SmartCardManager.h"

#include <iostream>
/*#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>*/

namespace smartCardManager
{
	// Create function
	void SmartCardManager::AssignSystemEventHandler()
	{
		ScrRegister(SmartCardManager::SystemCallback, 0);
	}
	void SmartCardManager::ClearSystemEventHandler()
	{
		ScrRegister(0, 0);
	}

	// constructor specifies slot
	CardReader::CardReader(WORD slotId)
	{
		iccID = slotId;
	}

	// public destructor
	CardReader::~CardReader()
	{
		// SmartCardManager is in charge of slot power
	}

	void SmartCardManager::SystemCallback(WORD argSlotID, void* argUserData, DWORD argEventID)
	{
		if (argSlotID < smartCardManager::SlotId::LAST_ENTRY)
		{
			SmartCardManager manager;
			if (argSlotID == SlotId::ICC)
			{
				manager.FrontSlotCallback(argEventID);
			}
			else
			{
				manager.SamSlotCallback(argEventID);
			}
		}
	}

	HANDLE SmartCardManager::quantumThreadHandle = 0;
	volatile bool SmartCardManager::isFrontSlotOccupied = false;
	DWORD SmartCardManager::QuantumThread(LPVOID arg)
	{
		// Get CardReader for the front slot
		smartCardManager::SmartCardManager cardManager;
		smartCardManager::CardReader frontSlot(cardManager.getFrontSlot(smartCardManager::SmartCardType::CARD_FOUND));

		// Try to reset the front slot
		Buffer<BYTE> reply;
		if (frontSlot.WarmReset(reply))
		{
			// Card Inserted
			isFrontSlotOccupied = true;

			// Wait for initialization
			Quantum::QuantumControl* quantumControl = Resources::GetQuantumControlInstance();
			int sleepCounter = 3000; // 30 seconds
			while (!quantumControl->isInitialised() && isFrontSlotOccupied && (--sleepCounter > 0))
			{
				Sleep(10);
			}
			// Provide a time out to stop thread hanging around indefinitely.
			if (sleepCounter == 0)
			{
				Display::DebugPrompt("Quantum", "Thread Timed Out", 30);
				quantumThreadHandle = 0;
				return 0;
			}
			// Process Sale
			std::string saleResult = "Sale Failed";
			if (quantumControl->transaction())
			{
				saleResult = "Sale Successful";
			}
			// Report Result
			if (isFrontSlotOccupied)
			{
				Display::DebugPrompt(saleResult, "Remove Card", 30);
			}
			else
			{
				cardManager.freeFrontSlot();
				Display::DebugPrompt(saleResult, "No Card", 30);
			}
		}
		else
		{
			// Card Removed
			isFrontSlotOccupied = false;

			cardManager.freeFrontSlot();
			Display::DebugPrompt("Quantum", "Card Removed");
		}
		quantumThreadHandle = 0;
		return 0;
	}

	// Only call from functions below
	// Must have QuantumThreadLock set
	// Zero the handle if the thread has terminated.
	void SmartCardManager::QuantumThreadHandleCheck()
	{
		// Check if quantum thread handle is valid
		if (quantumThreadHandle != 0)
		{
			// Check if quantumThread is still active
			if (HasThreadTerminated(quantumThreadHandle))
			{
				quantumThreadHandle = 0;
			}
		}
	}

	// Only call from functions below
	// Must have QuantumThreadLock set
	// Actually start the thread
	void SmartCardManager::StartQuantumThreadInner(bool detected)
	{
		// Start quantum sale transaction on its own thread
		// Only if there is no other thread
		if (quantumThreadHandle == 0)
		{
			quantumThreadHandle = CreateThread(0, 0, QuantumThread, 0, 0, 0);
			if (isFrontSlotOccupied)
			{
				if (detected)
				{
					Display::DebugPrompt("Quantum", "Card Detected", 30);
				}
				else
				{
					Display::DebugPrompt("Quantum", "Processing", 30);
				}
			}
		}
	}
	// Start the thread after card insertion
	void SmartCardManager::StartQuantumThread()
	{
		Resources::ResourceTryLock lock(Resources::ResourceLock::QuantumThreadLock);

		if (lock.HasLock())
		{
			QuantumThreadHandleCheck();
			if (quantumThreadHandle == 0)
			{
				StartQuantumThreadInner(false);
			}
		}
	}
	// Start the thread if a card is detected
	bool SmartCardManager::StartQuantumThreadIfCard()
	{
		Resources::ResourceTryLock lock(Resources::ResourceLock::QuantumThreadLock);

		bool result = false;
		if (lock.HasLock())
		{
			QuantumThreadHandleCheck();
			if (quantumThreadHandle == 0)
			{
				smartCardManager::SmartCardManager cardManager;
				smartCardManager::CardReader frontSlot(cardManager.getFrontSlot(smartCardManager::SmartCardType::CARD_FOUND));
				Buffer<BYTE> reply;
				if (frontSlot.WarmReset(reply))
				{
					result = true;
					StartQuantumThreadInner(true);
				}
				else
				{
					// Thread had control, but detected no card
					Display::DebugPrompt("Quantum", "Insert Card", 30);
				}
			}
		}
		return result;
	}

	void SmartCardManager::FrontSlotCallback(DWORD argEventID)
	{
		switch (argEventID)
		{
		case SCR_EVENT_RESET_DONE:
		{
		}
		break;
		case SCR_EVENT_DATA_READY:
		{
			// this event handled with ScrWaitForEvent
			return;
		}
		break;
		case SCR_EVENT_CARD_REMOVED:
		{
		}
		break;
		case SCR_EVENT_PROTOCOL_ERR:
		{
		}
		break;
		case SCR_EVENT_POWER_FAILURE:
		{
		}
		break;
		case SCR_EVENT_PARITY_ERR:
		{
		}
		break;
		case SCR_EVENT_NO_RESPONSE:
		{
		}
		break;
		case SCR_EVENT_CT_STATUS:
		{
			// Generated when a card is inserted into or removed from the front slot
			isFrontSlotOccupied = !isFrontSlotOccupied;
			StartQuantumThread();
		}
		break;
		case SCR_EVENT_PPS_NEGOTIATED:
		{
		}
		break;
		case SCR_EVENT_DEV_ONLINE:
		{
		}
		break;
		case SCR_EVENT_DEV_OFFLINE:
		{
		}
		break;
		default:
			// unknown event
		{
			printf("Unknown card reader event 0x%x\n", argEventID);
		}
		break;
		}
	}

	void SmartCardManager::SamSlotCallback(DWORD argEventID)
	{
		bool success = false;
		Buffer<BYTE> data;

		switch (argEventID)
		{
		case SCR_EVENT_RESET_DONE:
		{
			// this event handled with ScrWaitForEvent
			return;
		}
		break;
		case SCR_EVENT_DATA_READY:
		{
			// this event handled with ScrWaitForEvent
			return;
		}
		break;
		case SCR_EVENT_CARD_REMOVED:
		{
		}
		break;
		case SCR_EVENT_PROTOCOL_ERR:
		{
		}
		break;
		case SCR_EVENT_POWER_FAILURE:
		{
		}
		break;
		case SCR_EVENT_PARITY_ERR:
		{
		}
		break;
		case SCR_EVENT_NO_RESPONSE:
		{
		}
		break;
		case SCR_EVENT_CT_STATUS:
		{
			// Insert card. Not needed for P6
		}
		break;
		case SCR_EVENT_PPS_NEGOTIATED:
		{
		}
		break;
		case SCR_EVENT_DEV_ONLINE:
		{
		}
		break;
		case SCR_EVENT_DEV_OFFLINE:
		{
		}
		break;
		default:
			// unknown event
		{
			printf("Unknown card reader event 0x%x\n", argEventID);
		}
		break;
		}
	}

	bool CardReader::StartCardReading()
	{
		DWORD retval;
		if ((retval = cardProtocol.Read(iccID)) == 0)
		{
			// success
			retval = readerVersioning.Read();
		}
		return (retval != 0);
	}

	bool CardReader::ReadData(Buffer<BYTE>& result)
	{
		int length = SaioInterface::SaioInterface::SaioScrGetDataLength();

		if (length > 0)
		{
			result.Allocate(length);
			int confirmLength = SaioInterface::SaioInterface::SaioScrGetData(result.Get());
			if (confirmLength != length)
			{
				// On error clear the result array
				result.Clear();
				length = 0;
			}
		}
		else
		{
			result.Clear();
		}

		return (length > 0);
	}

	bool CardReader::ApplyApdu(const Buffer<BYTE>& apduInput, Buffer<BYTE>& apduOutput)
	{
		DWORD retval = SaioInterface::SaioInterface::SaioScrSendData(iccID, apduInput.Get(), apduInput.GetLength());
		if (retval != 0)
		{
			apduOutput.Clear();
			return false;
		}

		DWORD waitEventID = 0;
		retval = ScrWaitForEvent(&waitEventID, 2000);

		bool result = false;
		if ((retval == 0) && (waitEventID == SCR_EVENT_DATA_READY))
		{
			result = ReadData(apduOutput);
		}
		else
		{
			apduOutput.Clear();
		}

		return result;
	}

	bool CardReader::ApplyApduSync(const Buffer<BYTE>& apduInput, Buffer<BYTE>& apduOutput)
	{
		// Applies the apdu using syncronous command
		if (apduOutput.GetLength() == 0)
		{
			// default size
			apduOutput.Allocate(100);
		}
		WORD request = SCR_SYNC_EXCHANGE;
		DWORD resultLength = apduOutput.GetLength();

		//DumpData("ApplyApduSync: before: ", apduInput);

		DWORD retval = ScrSyncOpt(iccID, request,
			apduInput.Get(), apduInput.GetLength(),
			apduOutput.Get(), &resultLength);

		//DumpData("ApplyApduSync: after: ", apduOutput);

		apduOutput.Resize(resultLength);
		return (retval == 0);
	}

	// Returns true if a card is detected
	bool CardReader::WaitForResetEvent(Buffer<BYTE>& reply, DWORD timeout)
	{
		bool isCardPowered = false;
		// wait for power on event
		// SCR_EVENT_CARD_REMOVED event is generated for an empty slot
		// or sometimes EVENT_NO_RESPONSE - indicates T103 requires hard reset
		DWORD waitEventID = 0;
		DWORD retval = ScrWaitForEvent(&waitEventID, timeout);
		if (retval == SCR_OPT_TIMEOUT)
		{
			isCardPowered = false;
			return false;
		}

		isCardPowered = false;
		// Get Answer to reset event
		if (waitEventID == SCR_EVENT_RESET_DONE)
		{
			bool result = ReadData(reply);
			if (result)
			{
				isCardPowered = true;
			}
		}

		// Expected event when slot is empty
		// SCR_EVENT_NO_RESPONSE)
		// SCR_EVENT_CARD_REMOVED

		return isCardPowered;
	}

	// Returns true if successful
	bool CardReader::PowerOnSlot(Buffer<BYTE>& reply, DWORD timeout)
	{
		bool isSlotPowered = false;
		// Clear out targets
		bool result = false;
		reply.Clear();

		// Call power on system call
		DWORD retval = ScrPowerOn(iccID, 0);
		if (retval == 0)
		{
			// The ScrPowerOn command succeeded so ScrPowerOff required on exit
			SmartCardManager::PowerOn((SlotId::SlotId)iccID);
			isSlotPowered = true;
		}

		WaitForResetEvent(reply, timeout);

		// Return buffer
		return isSlotPowered;
	}
	// Returns true if successful, and card is powered
	bool CardReader::PowerOnAndReset(Buffer<BYTE>& reply, DWORD timeout)
	{
		// Clear out targets
		bool result = false;
		reply.Clear();

		// Call power on system call
		DWORD retval = ScrPowerOn(iccID, 0);
		if (retval == 0)
		{
			// The ScrPowerOn command succeeded so ScrPowerOff required on exit
			SmartCardManager::PowerOn((SlotId::SlotId)iccID);
		}

		bool isCardPowered = WaitForResetEvent(reply, timeout);

		return isCardPowered;
	}

	bool CardReader::WarmReset(Buffer<BYTE>& reply, DWORD timeout)
	{
		bool result = false;
		reply.Clear();

		DWORD retval = ScrReset(iccID);

		if (retval == 0)
		{
			result = WaitForResetEvent(reply, timeout);
		}

		return result;
	}

	bool CardReader::PowerOffSlot()
	{
		DWORD retval = 0;
		if (IsSlotPowered())
		{
			retval = ScrPowerOff(iccID);
			SmartCardManager::PowerOff((SlotId::SlotId)iccID);
		}
		return (retval == 0);
	}

	bool CardReader::IsSlotPowered() const
	{
		return SmartCardManager::GetPower((SlotId::SlotId)iccID);
	}

#if 0
	void CardReader::Print()
	{
		printf("CardReader slot %s card %s\n", isSlotPowered ? "Powered" : "Not Powered", isCardPowered ? "Powered" : "Not Powered");

		printf("Protocol %d %d %d %d\n", cardProtocol.icCardType, cardProtocol.iccProtocol,
			cardProtocol.iccClockRateConversionFactor, cardProtocol.dataBitRateAdjustmentFactor);

		printf("Reader versions 0x%x 0x%x 0x%x\n", readerVersioning.hardwareVersion, readerVersioning.driverVersion,
			readerVersioning.apiVersion);
	}

	void CardReader::DumpData(std::string message, const Buffer<BYTE>& data)
	{
		int length = data.GetLength();
		printf("%s Dump %d bytes", message.c_str(), length);
		for (int index = 0; index < length; index++)
		{
			if ((index % 20) == 0)
			{
				printf("\n");
			}
			printf(" %x", data[index]);
		}
		printf("\n");
	}
#endif

	std::string CardReader::DecodeState(DWORD state)
	{
		if (state == SCR_POWER_ON)
		{
			return "Power On";
		}
		if (state == SCR_POWER_OFF)
		{
			return "Power Off";
		}
		if (state == SCR_CARD_ABSENT)
		{
			return "Card Absent";
		}
		return "State Unknown";
	}

	int CardReader::GetSlotId() const
	{
		return iccID;
	}

	DWORD CardReader::GetStatus() const
	{
		DWORD status = 0;
		DWORD result;

		result = ScrGetState(iccID, &status);

		if (result != 0)
		{
			status = 0; // Failure result
		}

		return status;
	}

	// Return true if the thread handle has terminated.
	bool SmartCardManager::HasThreadTerminated(HANDLE threadHandle)
	{
		DWORD result = WaitForSingleObject(threadHandle, 0);
		// if the thread handle is signaled - the thread has terminated
		return (result == WAIT_OBJECT_0);
	}
}
