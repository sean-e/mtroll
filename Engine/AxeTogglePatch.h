/*
 * mTroll MIDI Controller
 * Copyright (C) 2010-2012,2014,2017-2018,2020 Sean Echevarria
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

#ifndef AxeTogglePatch_h__
#define AxeTogglePatch_h__

#include "TogglePatch.h"
#include "IAxeFx.h"


// AxeTogglePatch
// -----------------------------------------------------------------------------
// responds to SwitchPressed; SwitchReleased does not affect patch state
// supports expression pedals (psAllowOnlyActive) - but should it?
//
class AxeTogglePatch : public TogglePatch
{
	IAxeFxPtr		mAx = nullptr;
	bool			mHasDisplayText;
	int				mIsScene;
	std::string		mActiveText;
	std::string		mInactiveText;

public:
	AxeTogglePatch(int number, 
				const std::string & name, 
				IMidiOutPtr midiOut, 
				PatchCommands & cmdsA, 
				PatchCommands & cmdsB,
				IAxeFxPtr axeMgr,
				int isScenePatch) :
		TogglePatch(number, name, midiOut, cmdsA, cmdsB),
		mAx(axeMgr),
		mHasDisplayText(false),
		mIsScene(isScenePatch)
	{
		if (isScenePatch)
		{
			mPatchSupportsDisabledState = true;
			return;
		}

		if (mAx && mAx->GetModel() >= Axe2)
			mPatchSupportsDisabledState = true;

		if (mAx && mAx->GetModel() != Axe3)
		{
			std::string baseEffectName(name);
			std::string xy(" x/y");
			int xyPos = -1;
			xyPos = baseEffectName.find(xy);
			if (-1 == xyPos)
			{
				xy = " X/Y";
				xyPos = baseEffectName.find(xy);
			}

			if (-1 != xyPos)
			{
				mHasDisplayText = true;
				baseEffectName.replace(xyPos, xy.length(), "");
				// originally, I had X as active and Y as inactive but I prefer
				// LED off for X and on for Y
				mActiveText = baseEffectName + " Y";
				mInactiveText = baseEffectName + " X";
				// swap commands to support inverted LED behavior for X and Y.
				// see also UpdateState call in AxeFxManager::ReceivePresetEffectsV2
				// for the other change required to support LED inversion for X/Y.
				mCmdsA.swap(mCmdsB);
			}
		}
	}

	virtual ~AxeTogglePatch() = default;

	virtual void UpdateDisplays(IMainDisplay * mainDisplay, ISwitchDisplay * switchDisplay) const override
	{
		__super::UpdateDisplays(mainDisplay, switchDisplay);

		if (!mIsScene)
			return;

		// this causes preset and scene state to appear during for example MidiControlEngine::SwitchReleased_NavAndDescMode
		if (IsActive())
		{
			if (!mCmdsA.empty())
				UpdateAxeMgr();
		}
		else
		{
			if (!mCmdsB.empty())
				UpdateAxeMgr();
		}
	}

	virtual const std::string & GetDisplayText(bool checkState /*= false*/) const override
	{ 
		if (mHasDisplayText)
		{
			if (IsActive())
				return mActiveText;
			else
				return mInactiveText; 
		}

		return TogglePatch::GetDisplayText(checkState);
	}

	virtual bool HasDisplayText() const override { return mHasDisplayText; }
	
	virtual std::string GetPatchTypeStr() const override { return "axeToggle"; }

	virtual void ExecCommandsA() override
	{
		__super::ExecCommandsA();

		if (!mCmdsA.empty())
			UpdateAxeMgr();
	}

	virtual void ExecCommandsB() override
	{
		__super::ExecCommandsB();

		if (!mCmdsB.empty())
			UpdateAxeMgr();
	}

	void ClearAxeMgr() 
	{
		if (mAx)
			mAx = nullptr;
	}

private:
	void UpdateAxeMgr() const
	{
		if (!mAx)
			return;

		if (mIsScene)
			mAx->UpdateSceneStatus(mIsScene - 1, true);
		else
		{
			// Due to getting a response from the Axe-Fx II before state of
			// externals was accurate (Feedback Return mute mapped to Extern
			// 8 came back inaccurate when SyncEffectsFromAxe called immediately).
			mAx->DelayedEffectsSyncFromAxe();
		}
	}
};

#endif // AxeTogglePatch_h__
