/*
 * mTroll MIDI Controller
 * Copyright (C) 2010,2015 Sean Echevarria
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

#ifndef ITrollApplication_h__
#define ITrollApplication_h__


// ITrollApplication
// ----------------------------------------------------------------------------
//
class ITrollApplication
{
public:
	enum ExitAction { soeExit, soeExitAndSleep, soeExitAndHibernate };
	virtual void Reconnect() = 0;
	virtual void ToggleTraceWindow() = 0;
	virtual bool IsAdcOverridden(int adc) = 0;
	virtual void ToggleAdcOverride(int adc) = 0;
	virtual bool EnableTimeDisplay(bool enable) = 0;
	virtual std::string ApplicationDirectory() = 0;
	virtual std::string GetElapsedTimeStr() = 0;
	virtual void PauseOrResumeTime() = 0;
	virtual void ResetTime() = 0;
	virtual void Exit(ExitAction action) = 0;
};

#endif // ITrollApplication_h__
