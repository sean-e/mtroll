#include <algorithm>
#include <strstream>
#include "MidiControlEngine.h"
#include "PatchBank.h"
#include "IMainDisplay.h"
#include "ISwitchDisplay.h"
#include "ITraceDisplay.h"



template<typename T>
struct DeletePtr
{
	void operator()(const T * ptr)
	{
		delete ptr;
	}
};

struct DeletePatch
{
	void operator()(const std::pair<int, Patch *> & pr)
	{
		delete pr.second;
	}
};

static bool
SortByBankNumber(const PatchBank* lhs, const PatchBank* rhs)
{
	return lhs->GetBankNumber() < rhs->GetBankNumber();
}

// bad hardcoded switch grid assumptions - these need to come from one of the xml files...
const int kModeDefaultSwitchNumber = 0;
const int kModeBankNavSwitchNumber = 1;
const int kModeBankDescSwitchNumber = 2;
const int kModeBankDirect = 3;
const int kModeExprPedalDisplay = 4;

MidiControlEngine::MidiControlEngine(IMainDisplay * mainDisplay, 
									 ISwitchDisplay * switchDisplay, 
									 ITraceDisplay * traceDisplay,
									 int incrementSwitchNumber,
									 int decrementSwitchNumber,
									 int modeSwitchNumber) :
	mMainDisplay(mainDisplay),
	mSwitchDisplay(switchDisplay),
	mTrace(traceDisplay),
	mMode(emCreated),
	mActiveBank(NULL),
	mActiveBankIndex(0),
	mBankNavigationIndex(0),
	mPowerUpTimeout(0),
	mPowerUpBank(0),
	mPowerUpPatch(-1),
	mIncrementSwitchNumber(incrementSwitchNumber),
	mDecrementSwitchNumber(decrementSwitchNumber),
	mModeSwitchNumber(modeSwitchNumber),
	mFilterRedundantProgramChanges(false),
	mPedalModePort(0),
	mHistoryNavMode(hmNone)
{
	mBanks.reserve(999);
}

MidiControlEngine::~MidiControlEngine()
{
	std::for_each(mBanks.begin(), mBanks.end(), DeletePtr<PatchBank>());
	mBanks.clear();
	std::for_each(mPatches.begin(), mPatches.end(), DeletePatch());
	mPatches.clear();
}

PatchBank &
MidiControlEngine::AddBank(int number,
						   const std::string & name)
{
	PatchBank * pBank = new PatchBank(number, name);
	mBanks.push_back(pBank);
	return * pBank;
}

void
MidiControlEngine::AddPatch(Patch * patch)
{
	mPatches[patch->GetNumber()] = patch;	
}

void
MidiControlEngine::SetPowerup(int powerupBank,
							  int powerupPatch,
							  int powerupTimeout)
{
	mPowerUpPatch = powerupPatch;
	mPowerUpBank = powerupBank;
	mPowerUpTimeout = powerupTimeout;
}

void
MidiControlEngine::CompleteInit(const PedalCalibration * pedalCalibrationSettings)
{
	std::sort(mBanks.begin(), mBanks.end(), SortByBankNumber);

	PatchBank * defaultsBank = NULL;
	if (mBanks.begin() != mBanks.end())
	{
		defaultsBank = *mBanks.begin();
		// bank number 0 is used as the bank with the default mappings
		if (0 == defaultsBank->GetBankNumber())
			defaultsBank->InitPatches(mPatches); // init before calling SetDefaultMapping
		else
			defaultsBank = NULL;
	}

	int itIdx = 0;
	for (Banks::iterator it = mBanks.begin();
		it != mBanks.end();
		++it, ++itIdx)
	{
		PatchBank * curItem = *it;
		curItem->InitPatches(mPatches);
		if (defaultsBank)
			curItem->SetDefaultMappings(*defaultsBank);
	}

	CalibrateExprSettings(pedalCalibrationSettings);
	ChangeMode(emBank);
	
	if (mTrace)
	{
		std::strstream traceMsg;
		traceMsg << "Load complete: bank cnt " << mBanks.size() << ", patch cnt " << mPatches.size() << std::endl << std::ends;
		mTrace->Trace(std::string(traceMsg.str()));
	}

	LoadStartupBank();
}

void
MidiControlEngine::CalibrateExprSettings(const PedalCalibration * pedalCalibrationSettings)
{
	mGlobalPedals.Calibrate(pedalCalibrationSettings);

	for (Banks::iterator it = mBanks.begin();
		it != mBanks.end();
		++it)
	{
		PatchBank * curItem = *it;
		curItem->CalibrateExprSettings(pedalCalibrationSettings);
	}
}

void
MidiControlEngine::LoadStartupBank()
{
	int powerUpBankIndex = -1;
	int itIdx = 0;
	for (Banks::iterator it = mBanks.begin();
		it != mBanks.end();
		++it, ++itIdx)
	{
		PatchBank * curItem = *it;
		if (curItem->GetBankNumber() == mPowerUpBank)
		{
			powerUpBankIndex = itIdx;
			break;
		}
	}

	if (powerUpBankIndex == -1)
		powerUpBankIndex = 0;

	LoadBank(powerUpBankIndex);
}

void
MidiControlEngine::SwitchPressed(int switchNumber)
{
	if (0 && mTrace)
	{
		std::strstream msg;
		msg << "SwitchPressed: " << switchNumber << std::endl << std::ends;
		mTrace->Trace(std::string(msg.str()));
	}

	if (emBank == mMode)
	{
		if (switchNumber == mIncrementSwitchNumber ||
			switchNumber == mDecrementSwitchNumber)
			ChangeMode(emBankNav);
		else if (switchNumber == mModeSwitchNumber)
			;
		else if (mActiveBank)
			mActiveBank->PatchSwitchPressed(switchNumber, mMainDisplay, mSwitchDisplay);
		return;
	}
}

void
MidiControlEngine::SwitchReleased(int switchNumber)
{
	if (0 && mTrace)
	{
		std::strstream msg;
		msg << "SwitchReleased: " << switchNumber << std::endl << std::ends;
		mTrace->Trace(msg.str());
	}

	if (emCreated == mMode)
		return;

	if (emBank == mMode)
	{
		if (switchNumber == mIncrementSwitchNumber ||
			switchNumber == mDecrementSwitchNumber)
		{
			return;
		}

		if (switchNumber == mModeSwitchNumber)
		{
			ChangeMode(emModeSelect);
			return;
		}

		if (mActiveBank)
			mActiveBank->PatchSwitchReleased(switchNumber, mMainDisplay, mSwitchDisplay);

		return;
	}

	if (emBankNav == mMode ||
		emBankDesc == mMode)
	{
		if (switchNumber == mIncrementSwitchNumber)
		{
			// bank inc/dec does not commit bank
			NavigateBankRelative(1);
		}
		else if (switchNumber == mDecrementSwitchNumber)
		{
			// bank inc/dec does not commit bank
			NavigateBankRelative(-1);
		}
		else if (switchNumber == mModeSwitchNumber)
		{
			// escape
			ChangeMode(emBank);
			mBankNavigationIndex = mActiveBankIndex;
			NavigateBankRelative(0);
		}
		else if (emBankNav == mMode)
		{
			// any switch release (except inc/dec/util) after bank inc/dec commits bank
			// reset to default mode when in bankNav mode
			ChangeMode(emBank);
			LoadBank(mBankNavigationIndex);
		}
		else if (emBankDesc == mMode)
		{
			PatchBank * bank = GetBank(mBankNavigationIndex);
			if (bank)
			{
				bank->DisplayInfo(mMainDisplay, mSwitchDisplay, true, true);
				bank->DisplayDetailedPatchInfo(switchNumber, mMainDisplay);
			}
		}

		return;
	}

	if (emModeSelect == mMode)
	{
		if (switchNumber == mIncrementSwitchNumber ||
			switchNumber == mDecrementSwitchNumber)
		{
			return;
		}
		else if (switchNumber == mModeSwitchNumber ||
			switchNumber == kModeDefaultSwitchNumber)
		{
			// escape
			ChangeMode(emBank);
			mBankNavigationIndex = mActiveBankIndex;
			NavigateBankRelative(0);
		}
		else if (switchNumber == kModeBankDescSwitchNumber)
		{
			ChangeMode(emBankDesc);
			mBankNavigationIndex = mActiveBankIndex;
			NavigateBankRelative(0);
		}
		else if (switchNumber == kModeBankNavSwitchNumber)
		{
			ChangeMode(emBankNav);
			mBankNavigationIndex = mActiveBankIndex;
			NavigateBankRelative(0);
		}
		else if (switchNumber == kModeBankDirect)
		{
			ChangeMode(emBankDirect);
		}
		else if (switchNumber == kModeExprPedalDisplay)
		{
			ChangeMode(emExprPedalDisplay);
		}

		return;
	}

	if (emExprPedalDisplay == mMode)
	{
		if (switchNumber == mIncrementSwitchNumber ||
			switchNumber == mDecrementSwitchNumber)
		{
			return;
		}
		else if (switchNumber == mModeSwitchNumber)
		{
			// escape
			ChangeMode(emBank);
			mBankNavigationIndex = mActiveBankIndex;
			NavigateBankRelative(0);
		}
		else if (switchNumber >= 0 && switchNumber <= 3)
		{
			mPedalModePort = switchNumber;
			if (mMainDisplay)
			{
				std::strstream displayMsg;
				displayMsg << "Displaying ADC values for port " << (int) (mPedalModePort + 1) << std::endl << std::ends;
				mMainDisplay->TextOut(displayMsg.str());
			}
		}

		return;
	}

	if (emBankDirect == mMode)
	{
		bool updateMainDisplay = true;
		switch (switchNumber)
		{
		case 0:		mBankDirectNumber += "1";	break;
		case 1:		mBankDirectNumber += "2";	break;
		case 2:		mBankDirectNumber += "3";	break;
		case 3:		mBankDirectNumber += "4";	break;
		case 4:		mBankDirectNumber += "5";	break;
		case 5:		mBankDirectNumber += "6";	break;
		case 6:		mBankDirectNumber += "7";	break;
		case 7:		mBankDirectNumber += "8";	break;
		case 8:		mBankDirectNumber += "9";	break;
		case 9:		mBankDirectNumber += "0";	break;
		}

		if (switchNumber == mModeSwitchNumber)
		{
			// escape
			ChangeMode(emBank);
			mBankNavigationIndex = mActiveBankIndex;
			NavigateBankRelative(0);
			updateMainDisplay = false;
		}
		else if (switchNumber == mDecrementSwitchNumber)
		{
			// remove last char
			if (mBankDirectNumber.length())
				mBankDirectNumber = mBankDirectNumber.erase(mBankDirectNumber.length() - 1);
		}
		else if (switchNumber == mIncrementSwitchNumber)
		{
			// commit
			ChangeMode(emBank);
			mBankNavigationIndex = mActiveBankIndex;
			const int bnkIdx = GetBankIndex(::atoi(mBankDirectNumber.c_str()));
			if (bnkIdx != -1)
				LoadBank(bnkIdx);
			else if (mMainDisplay)
				mMainDisplay->TextOut("Invalid bank number");
			updateMainDisplay = false;
		}

		if (mMainDisplay && updateMainDisplay)
		{
			const int bnkIdx = GetBankIndex(::atoi(mBankDirectNumber.c_str()));
			if (bnkIdx == -1)
			{
				mMainDisplay->TextOut(mBankDirectNumber + " (invalid bank number)");
			}
			else
			{
				PatchBank * bnk = GetBank(bnkIdx);
				_ASSERTE(bnk);
				mMainDisplay->TextOut(mBankDirectNumber + " " + bnk->GetBankName());
			}
		}

		return;
	}
}

void
MidiControlEngine::AdcValueChanged(int port, 
								   int newValue)
{
	_ASSERTE(port < ExpressionPedals::PedalCount);
	if (emExprPedalDisplay == mMode)
	{
		if (mMainDisplay && mPedalModePort == port)
		{
			std::strstream displayMsg;
			displayMsg << "ADC port " << (int) (port+1) << " value: " << newValue << std::endl << std::ends;
			mMainDisplay->TextOut(displayMsg.str());
		}
		return;
	}

	// forward directly to active patch
	if (!gActivePatchPedals || 
		gActivePatchPedals->AdcValueChange(mMainDisplay, port, newValue))
	{
		// process globals if no rejection
		mGlobalPedals.AdcValueChange(mMainDisplay, port, newValue);
	}
}

void
MidiControlEngine::ResetBankPatches()
{
	if (mActiveBank)
		mActiveBank->ResetPatches(mMainDisplay, mSwitchDisplay);
}

bool
MidiControlEngine::NavigateBankRelative(int relativeBankIndex)
{
	// bank inc/dec does not commit bank
	const int kBankCnt = mBanks.size();
	if (!kBankCnt || kBankCnt == 1)
		return false;

	mBankNavigationIndex = mBankNavigationIndex + relativeBankIndex;
	if (mBankNavigationIndex < 0)
		mBankNavigationIndex = kBankCnt - 1;
	if (mBankNavigationIndex >= kBankCnt)
		mBankNavigationIndex = 0;

	if (mSwitchDisplay)
	{
		for (int idx = 0; idx < 64; idx++)
		{
			if (idx != mModeSwitchNumber &&
				idx != mDecrementSwitchNumber &&
				idx != mIncrementSwitchNumber)
			{
				mSwitchDisplay->ClearSwitchText(idx);
				mSwitchDisplay->SetSwitchDisplay(idx, false);
			}
		}
	}

	// display bank info
	PatchBank * bank = GetBank(mBankNavigationIndex);
	if (!bank)
		return false;

	bank->DisplayInfo(mMainDisplay, mSwitchDisplay, true, relativeBankIndex != 0);
	return true;
}

void
MidiControlEngine::LoadBankByNumber(int bankNumber)
{
	int bankidx = GetBankIndex(bankNumber);
	if (-1 == bankidx)
		return;

	ChangeMode(emBank);
	LoadBank(bankidx);

	PatchBank * bank = GetBank(bankidx);
	if (!bank)
		return;

	bank->DisplayInfo(mMainDisplay, mSwitchDisplay, true, false);
}

int
MidiControlEngine::GetBankIndex(int bankNumber)
{
	int idx = 0;
	for (Banks::iterator it = mBanks.begin();
		it != mBanks.end();
		++it, ++idx)
	{
		PatchBank * curItem = *it;
		if (curItem->GetBankNumber() == bankNumber)
			return idx;
	}
	return -1;
}

PatchBank *
MidiControlEngine::GetBank(int bankIndex)
{
	const int kBankCnt = mBanks.size();
	if (bankIndex < 0 || bankIndex >= kBankCnt)
		return NULL;

	PatchBank * bank = mBanks[bankIndex];
	return bank;
}

bool
MidiControlEngine::LoadBank(int bankIndex)
{
	PatchBank * bank = GetBank(bankIndex);
	if (!bank)
		return false;

	if (mActiveBank)
		mActiveBank->Unload(mMainDisplay, mSwitchDisplay);

	mActiveBank = bank;

	if (hmWentBack == mHistoryNavMode || 
		hmWentForward == mHistoryNavMode)
	{
		// leave recall mode
		mHistoryNavMode = hmNone;
	}

	if (hmNone == mHistoryNavMode)
	{
		// record bank load for Back
		if (mBackHistory.empty() || mBackHistory.top() != mActiveBankIndex)
		{
			mBackHistory.push(mActiveBankIndex);

			// invalidate Forward hist
			while (!mForwardHistory.empty())
				mForwardHistory.pop();
		}
	}

	mBankNavigationIndex = mActiveBankIndex = bankIndex;
	mActiveBank->Load(mMainDisplay, mSwitchDisplay);
	UpdateBankModeSwitchDisplay();
	
	return true;
}

void
MidiControlEngine::HistoryBackward()
{
	if (mBackHistory.empty())
		return;

	const int bankIdx = mBackHistory.top();
	mBackHistory.pop();
	mForwardHistory.push(mActiveBankIndex);

	mHistoryNavMode = hmBack;
	LoadBank(bankIdx);
	mHistoryNavMode = hmWentBack;

	PatchBank * bank = GetBank(bankIdx);
	if (!bank)
		return;

	bank->DisplayInfo(mMainDisplay, mSwitchDisplay, true, false);
}

void
MidiControlEngine::HistoryForward()
{
	if (mForwardHistory.empty())
		return;
	
	const int bankIdx = mForwardHistory.top();
	mForwardHistory.pop();
	mBackHistory.push(mActiveBankIndex);

	mHistoryNavMode = hmForward;
	LoadBank(bankIdx);
	mHistoryNavMode = hmWentForward;

	PatchBank * bank = GetBank(bankIdx);
	if (!bank)
		return;

	bank->DisplayInfo(mMainDisplay, mSwitchDisplay, true, false);
}

void
MidiControlEngine::HistoryRecall()
{
	switch (mHistoryNavMode)
	{
	case hmNone:
	case hmWentForward:
		HistoryBackward();
		break;
	case hmWentBack:
		HistoryForward();
	    break;
	case hmBack:
	case hmForward:
		_ASSERTE(!"invalid history nav mode");
		break;
	}
}

// possible mode transitions:
// emCreated -> emDefault
// emDefault -> emBankNav
// emDefault -> emModeSelect
// emModeSelect -> emDefault
// emModeSelect -> emBankNav
// emModeSelect -> emBankDesc
// emModeSelect -> emBankDirect
// emModeSelect -> emExprPedalDisplay
// emBankNav -> emDefault
// emBankDesc -> emDefault
// emBankDirect -> emDefault
// emExprPedalDisplay -> emDefault
void
MidiControlEngine::ChangeMode(EngineMode newMode)
{
	mMode = newMode;

	if (mSwitchDisplay)
	{
		for (int idx = 0; idx < 64; idx++)
		{
			mSwitchDisplay->ClearSwitchText(idx);
			mSwitchDisplay->SetSwitchDisplay(idx, false);
		}
	}

	bool showModeInMainDisplay = true;
	std::string msg;
	switch (mMode)
	{
	case emBank:
		msg = "Bank";
		if (mSwitchDisplay)
		{
			mSwitchDisplay->SetSwitchText(mIncrementSwitchNumber, "Next Bank");
			mSwitchDisplay->SetSwitchText(mDecrementSwitchNumber, "Prev Bank");
		}

		if (mActiveBank)
		{
			// caller changing to emBank will update mainDisplay - reduce flicker
			showModeInMainDisplay = false;
			UpdateBankModeSwitchDisplay();
			if (mSwitchDisplay)
				msg.clear();
		}
		break;
	case emBankNav:
		msg = "Bank Navigation";
		if (mActiveBank)
			showModeInMainDisplay = false;
		if (mSwitchDisplay)
		{
			mSwitchDisplay->SetSwitchText(mIncrementSwitchNumber, "Next Bank");
			mSwitchDisplay->SetSwitchText(mDecrementSwitchNumber, "Prev Bank");
		}
		break;
	case emBankDesc:
		msg = "Bank and Switch Description";
		if (mSwitchDisplay)
		{
			mSwitchDisplay->SetSwitchText(mIncrementSwitchNumber, "Next Bank");
			mSwitchDisplay->SetSwitchText(mDecrementSwitchNumber, "Prev Bank");
		}
		break;
	case emModeSelect:
		msg = "Mode Select";
		if (mSwitchDisplay)
		{
			mSwitchDisplay->SetSwitchText(kModeDefaultSwitchNumber, "Bank");
			mSwitchDisplay->SetSwitchText(kModeBankNavSwitchNumber, "Bank Navigation");
			mSwitchDisplay->SetSwitchText(kModeBankDescSwitchNumber, "Bank Description");
			mSwitchDisplay->SetSwitchText(kModeBankDirect, "Bank Direct");
			mSwitchDisplay->SetSwitchText(kModeExprPedalDisplay, "Raw ADC Values");
		}
		break;
	case emExprPedalDisplay:
		msg = "Raw ADC values";
		mPedalModePort = 0;
		if (mSwitchDisplay)
		{
			mSwitchDisplay->SetSwitchText(mIncrementSwitchNumber, "");
			mSwitchDisplay->SetSwitchText(mDecrementSwitchNumber, "");
			mSwitchDisplay->SetSwitchText(0, "Pedal 1");
			mSwitchDisplay->SetSwitchText(1, "Pedal 2");
			mSwitchDisplay->SetSwitchText(2, "Pedal 3");
			mSwitchDisplay->SetSwitchText(3, "Pedal 4");
		}
		break;
	case emBankDirect:
		mBankDirectNumber.clear();
		msg = "Bank Direct";
		if (mSwitchDisplay)
		{
			mSwitchDisplay->SetSwitchText(0, "1");
			mSwitchDisplay->SetSwitchText(1, "2");
			mSwitchDisplay->SetSwitchText(2, "3");
			mSwitchDisplay->SetSwitchText(3, "4");
			mSwitchDisplay->SetSwitchText(4, "5");
			mSwitchDisplay->SetSwitchText(5, "6");
			mSwitchDisplay->SetSwitchText(6, "7");
			mSwitchDisplay->SetSwitchText(7, "8");
			mSwitchDisplay->SetSwitchText(8, "9");
			mSwitchDisplay->SetSwitchText(9, "0");
			mSwitchDisplay->SetSwitchText(mIncrementSwitchNumber, "Commit");
			mSwitchDisplay->SetSwitchText(mDecrementSwitchNumber, "Backspace");
		}
		break;
	default:
		msg = "Invalid";
	    break;
	}

	if (showModeInMainDisplay && mMainDisplay)
		mMainDisplay->TextOut("mode: " +  msg);

	if (mSwitchDisplay)
	{
		if (!msg.empty())
			mSwitchDisplay->SetSwitchText(mModeSwitchNumber, msg);
		mSwitchDisplay->SetSwitchDisplay(mModeSwitchNumber, mMode == emBank ? true : false);
	}
}

void
MidiControlEngine::UpdateBankModeSwitchDisplay()
{
	if (!mSwitchDisplay)
		return;

	_ASSERTE(emBank == mMode);
	if (mActiveBank)
	{
		std::strstream msg;
		msg << mActiveBank->GetBankNumber() << ": " << mActiveBank->GetBankName() << std::endl << std::ends;
		mSwitchDisplay->SetSwitchText(mModeSwitchNumber, msg.str());
	}
	else
	{
		mSwitchDisplay->SetSwitchText(mModeSwitchNumber, "Bank");
	}
}
