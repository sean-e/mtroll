/*
 * mTroll MIDI Controller
 * Copyright (C) 2007-2012 Sean Echevarria
 *
 * This file is part of mTroll.
 *
 * mTroll is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mTroll is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Let me know if you modify, extend or use mTroll.
 * Original project site: http://www.creepingfog.com/mTroll/
 * Contact Sean: "fester" at the domain of the original project site
 */

#ifndef MidiControlEngine_h__
#define MidiControlEngine_h__

#include <map>
#include <vector>
#include <stack>

#include "Patch.h"
#include "ExpressionPedals.h"
#include "../Monome40h/IMonome40hInputSubscriber.h"


class ITrollApplication;
class IMainDisplay;
class ISwitchDisplay;
class ITraceDisplay;
class IMidiOut;
class Patch;
class PatchBank;
class AxeFxManager;


class MidiControlEngine : public IMonome40hAdcSubscriber
{
public:
	MidiControlEngine(ITrollApplication * app,
					  IMainDisplay * mainDisplay, 
					  ISwitchDisplay * switchDisplay,
					  ITraceDisplay * traceDisplay,
					  IMidiOut * midiOut,
					  AxeFxManager * axMgr,
					  int incrementSwitchNumber,
					  int decrementSwitchNumber,
					  int modeSwitchNumber);
	~MidiControlEngine();

	// These are the switch numbers used in "Mode Select" mode (default values 
	// that can be overridden in the config xml file <switches> section)
	enum EngineModeSwitch
	{
		kUnassignedSwitchNumber = -1,
		kModeRecall = 0,
		kModeBack,
		kModeForward,
		kModeTime,
		kModeProgramChangeDirect,
		kModeControlChangeDirect,
		kModeBankDirect,
		kModeExprPedalDisplay,
		kModeAdcOverride,
		kModeTestLeds,
		kModeToggleTraceWindow,
		kModeBankDesc,
		kModeToggleLedInversion
	};

	// initialization
	typedef std::map<int, Patch*> Patches;
	PatchBank &				AddBank(int number, const std::string & name);
	void					AddPatch(Patch * patch);
	void					SetPowerup(int powerupBank, int powerupPatch, int powerupTimeout);
	void					FilterRedundantProgChg(bool filter) {mFilterRedundantProgramChanges = filter;}
	void					AssignCustomBankLoad(int switchNumber, int bankNumber);
	void					AssignModeSwitchNumber(EngineModeSwitch mode, int switchNumber);
	void					CompleteInit(const PedalCalibration * pedalCalibrationSettings);

	ExpressionPedals &		GetPedals() {return mGlobalPedals;}
	Patch *					GetPatch(int number);
	ISwitchDisplay *		GetSwitchDisplay() const { return mSwitchDisplay; }

	void					SwitchPressed(int switchNumber);
	void					SwitchReleased(int switchNumber);
	virtual void			AdcValueChanged(int port, int curValue);
	void					RefirePedal(int pedal);
	void					ResetBankPatches();
	void					LoadBankByNumber(int bankNumber);
	void					LoadBankRelative(int relativeBankIndex);

	void					EnterNavMode() { ChangeMode(emBankNav); }
	void					HistoryBackward();
	void					HistoryForward();
	void					HistoryRecall();

private:
	void					LoadStartupBank();
	bool					NavigateBankRelative(int relativeBankIndex);
	bool					LoadBank(int bankIndex);
	PatchBank *				GetBank(int bankIndex);
	int						GetBankIndex(int bankNumber);
	void					UpdateBankModeSwitchDisplay();
	void					CalibrateExprSettings(const PedalCalibration * pedalCalibrationSettings);
	void					EscapeToDefaultMode();
	int						GetSwitchNumber(EngineModeSwitch m) const;
	enum EngineMode 
	{ 
		emCreated = -1,		// initial state - no data loaded
		emBank,				// select presets in banks
		emModeSelect,		// out of default ready to select new mode
		emBankNav,			// navigate banks
		emBankDesc,			// describe switches in bank
		emBankDirect,		// use buttons to call bank
		emExprPedalDisplay,	// display actual adc values
		emAdcOverride,		// set ADC overrides
		emTimeDisplay,		// displays time
		emProgramChangeDirect, // manual send of program changes
		emControlChangeDirect, // manual send of control changes
		emNotValid 
	};
	void					ChangeMode(EngineMode newMode);
	void					SetupModeSelectSwitch(EngineModeSwitch m);
	void					SwitchReleased_AdcOverrideMode(int switchNumber);
	void					SwitchReleased_PedalDisplayMode(int switchNumber);
	void					SwitchReleased_NavAndDescMode(int switchNumber);
	void					SwitchReleased_ModeSelect(int switchNumber);
	void					SwitchReleased_BankDirect(int switchNumber);
	void					SwitchPressed_ProgramChangeDirect(int switchNumber);
	void					SwitchReleased_ProgramChangeDirect(int switchNumber);
	void					SwitchPressed_ControlChangeDirect(int switchNumber);
	void					SwitchReleased_ControlChangeDirect(int switchNumber);

private:
	// non-retained runtime state
	ITrollApplication *		mApplication;
	IMainDisplay *			mMainDisplay;
	ITraceDisplay *			mTrace;
	ISwitchDisplay *		mSwitchDisplay;
	IMidiOut *				mMidiOut; // only used for emProgramChangeDirect/emControlChangeDirect
	AxeFxManager *			mAxeMgr;

	PatchBank *				mActiveBank;
	int						mActiveBankIndex;
	EngineMode				mMode;
	int						mBankNavigationIndex;
	std::string				mDirectNumber; // used by emBankDirect / emProgramChangeDirect / emControlChangeDirect
	int						mDirectChangeChannel; // used by emProgramChangeDirect / emControlChangeDirect
	int						mDirectValueLastSent; // used by emProgramChangeDirect / emControlChangeDirect
	int						mDirectValue1LastSent; // used by emControlChangeDirect
	std::stack<int>			mBackHistory;
	std::stack<int>			mForwardHistory;
	enum HistoryNavMode		{ hmNone, hmBack, hmForward, hmWentBack, hmWentForward};
	HistoryNavMode			mHistoryNavMode;
	unsigned int			mSwitchPressedEventTime;

	// retained in different form
	Patches					mPatches;		// patchNum is key
	typedef std::vector<PatchBank*> Banks;
	Banks					mBanks;			// compressed; bankNum is not index

	// retained state
	int						mPowerUpTimeout;
	int						mPowerUpBank;
	int						mPowerUpPatch;
	bool					mFilterRedundantProgramChanges;
	ExpressionPedals		mGlobalPedals;
	int						mPedalModePort;

	// mode switch numbers
	int						mModeSwitchNumber;
	int						mIncrementSwitchNumber;
	int						mDecrementSwitchNumber;
	std::map<EngineModeSwitch, int>		mOtherModeSwitchNumbers;
	std::map<int, int>		mBankLoadSwitchNumbers;
};

#endif // MidiControlEngine_h__
