/*
 * mTroll MIDI Controller
 * Copyright (C) 2007-2008,2018 Sean Echevarria
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

#ifndef MetaPatch_ResetBankPatches_h__
#define MetaPatch_ResetBankPatches_h__

#include "Patch.h"
#include "MidiControlEngine.h"


class MetaPatch_ResetBankPatches : public Patch
{
public:
	MetaPatch_ResetBankPatches(MidiControlEngine * engine, int number, const std::string & name) : 
		Patch(number, name),
		mEngine(engine)
	{
		_ASSERTE(mEngine);
	}

	virtual std::string GetPatchTypeStr() const override {return "meta: resetBankPatches";}

	virtual void SwitchPressed(IMainDisplay *, ISwitchDisplay *) override
	{
		mEngine->ResetBankPatches();
	}

	virtual void BankTransitionActivation() override {SwitchPressed(nullptr, nullptr);}
	virtual void BankTransitionDeactivation() override {SwitchPressed(nullptr, nullptr);}

private:
	MidiControlEngine	* mEngine;
};

#endif // MetaPatch_ResetBankPatches_h__
