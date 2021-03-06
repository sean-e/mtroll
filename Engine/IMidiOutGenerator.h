/*
 * mTroll MIDI Controller
 * Copyright (C) 2007-2008,2014,2018,2020 Sean Echevarria
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

#ifndef IMidiOutGenerator_h__
#define IMidiOutGenerator_h__


class IMidiOut;

using IMidiOutPtr = std::shared_ptr<IMidiOut>;

// IMidiOutGenerator
// ----------------------------------------------------------------------------
// use to create IMidiOut
//
class IMidiOutGenerator
{
public:
	virtual IMidiOutPtr	CreateMidiOut(unsigned int deviceIdx, int activityIndicatorIdx, unsigned int ledColor) = 0;
	virtual IMidiOutPtr	GetMidiOut(unsigned int deviceIdx) = 0;
	virtual unsigned int GetMidiOutDeviceIndex(const std::string &deviceName) = 0;
	virtual void		OpenMidiOuts() = 0;
	virtual void		CloseMidiOuts() = 0;
};

#endif // IMidiOutGenerator_h__
