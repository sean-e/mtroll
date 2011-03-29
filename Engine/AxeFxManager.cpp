/*
 * mTroll MIDI Controller
 * Copyright (C) 2010-2011 Sean Echevarria
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

#include <string>
#include <memory.h>
#include <strstream>
#include <algorithm>
#include <QEvent>
#include <QApplication>
#include <QTimer>
#include "AxeFxManager.h"
#include "ITraceDisplay.h"
#include "HexStringUtils.h"
#include "IMidiIn.h"
#include "Patch.h"
#include "ISwitchDisplay.h"
#include "AxemlLoader.h"
#include "IMidiOut.h"
#include "IMainDisplay.h"


// Consider: restrict effect bypasses to mEffectIsPresentInAxePatch?

static void SynonymNormalization(std::string & name);

AxeFxManager::AxeFxManager(IMainDisplay * mainDisp, 
						   ISwitchDisplay * switchDisp,
						   ITraceDisplay *pTrace,
						   const std::string & appPath, 
						   int ch) :
	mSwitchDisplay(switchDisp),
	mMainDisplay(mainDisp),
	mTrace(pTrace),
	mRefCnt(0),
	mTimeoutCnt(0),
	mLastTimeout(0),
	mTempoPatch(NULL),
	// mQueryLock(QMutex::Recursive),
	mMidiOut(NULL),
	mCheckedFirmware(false),
	mPatchDumpBytesReceived(0),
	mAxeChannel(ch),
	mModel(0)
{
	mQueryTimer = new QTimer(this);
	connect(mQueryTimer, SIGNAL(timeout()), this, SLOT(QueryTimedOut()));
	mQueryTimer->setSingleShot(true);
	mQueryTimer->setInterval(2000);

	mDelayedSyncTimer = new QTimer(this);
	connect(mDelayedSyncTimer, SIGNAL(timeout()), this, SLOT(SyncFromAxe()));
	mDelayedSyncTimer->setSingleShot(true);
	mDelayedSyncTimer->setInterval(100);

	AxemlLoader ldr(mTrace);
	ldr.Load(appPath + "/default.axeml", mAxeEffectInfo);
}

AxeFxManager::~AxeFxManager()
{
	if (mDelayedSyncTimer->isActive())
		mDelayedSyncTimer->stop();
	delete mDelayedSyncTimer;
	mDelayedSyncTimer = NULL;

	QMutexLocker lock(&mQueryLock);
	if (mQueryTimer->isActive())
		mQueryTimer->stop();
	delete mQueryTimer;
	mQueryTimer = NULL;
}

void
AxeFxManager::AddRef()
{
	// this should only happen on the UI thread, otherwise mRefCnt needs 
	// to be thread protected
	++mRefCnt;
}

void
AxeFxManager::Release()
{
	// this should only happen on the UI thread, otherwise mRefCnt needs 
	// to be thread protected
	if (!--mRefCnt)
		delete this;
}

void
AxeFxManager::SetTempoPatch(Patch * patch)
{
	if (mTempoPatch && mTrace)
	{
		std::string msg("Warning: multiple Axe-Fx Tap Tempo patches\n");
		mTrace->Trace(msg);
	}
	mTempoPatch = patch;
}

bool
AxeFxManager::SetSyncPatch(Patch * patch, int bypassCc /*= -1*/)
{
	std::string normalizedEffectName(patch->GetName());
	::NormalizeAxeEffectName(normalizedEffectName);
	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		AxeEffectBlockInfo & cur = *it;
		if (cur.mNormalizedName == normalizedEffectName)
		{
			if (cur.mPatch && mTrace)
			{
				std::string msg("Warning: multiple Axe-Fx patches for " + patch->GetName() + "\n");
				mTrace->Trace(msg);
			}

			if (-1 != bypassCc)
				cur.mBypassCC = bypassCc;

			cur.mPatch = patch;
			return true;
		}
	}

	if (mTrace && 
		0 != normalizedEffectName.find("looper ") &&
		0 != normalizedEffectName.find("external ") &&
		normalizedEffectName != "tuner" &&
		normalizedEffectName != "global preset effect toggle" &&
		normalizedEffectName != "volume increment" &&
		normalizedEffectName != "volume decrement")
	{
		std::string msg("Warning: no Axe-Fx effect found to sync for " + patch->GetName() + "\n");
		mTrace->Trace(msg);
	}

	return false;
}

void
AxeFxManager::CompleteInit(IMidiOut * midiOut)
{
	mMidiOut = midiOut;
	if (!mMidiOut && mTrace)
	{
		const std::string msg("ERROR: no Midi Out set for AxeFx sync\n");
		mTrace->Trace(msg);
	}

	SendFirmwareVersionQuery();
}

void
AxeFxManager::Closed(IMidiIn * midIn)
{
	midIn->Unsubscribe(this);
	Release();
}

void
AxeFxManager::ReceivedData(byte b1, byte b2, byte b3)
{
// 	if (mTrace)
// 	{
// 		const std::string msg(::GetAsciiHexStr((byte*)&dwParam1, 4, true) + "\n");
// 		mTrace->Trace(msg);
// 	}
}

// f0 00 01 74 01 ...
bool
IsAxeFxSysex(const byte * bytes, const int len)
{
	if (len < 7)
		return false;

	const byte kAxeFx[] = { 0xf0, 0x00, 0x01, 0x74 };
	if (::memcmp(bytes, kAxeFx, 4))
		return false;

// 	const byte kAxeFxUltraId[]	= { 0xf0, 0x00, 0x01, 0x74, 0x01 };
// 	const byte kAxeFxStdId[]	= { 0xf0, 0x00, 0x01, 0x74, 0x00 };

	switch (bytes[4])
	{
	case 0:
	case 1:
		return true;
	}

	return false;
}

void
AxeFxManager::ReceivedSysex(const byte * bytes, int len)
{
	// http://www.fractalaudio.com/forum/viewtopic.php?f=14&t=21524&start=10
	// http://axefxwiki.guitarlogic.org/index.php?title=Axe-Fx_SysEx_Documentation
	if (!::IsAxeFxSysex(bytes, len))
	{
		if (mPatchDumpBytesReceived)
		{
			// continuation of previous sysex received
			ContinueReceivePatchDump(bytes, len);
		}
		else
			; // not Axe sysex
		return;
	}

	switch (bytes[5])
	{
	case 0x10:
		// Tempo: f0 00 01 74 01 10 f7
		if (mTempoPatch)
		{
			mTempoPatch->ActivateSwitchDisplay(mSwitchDisplay, true);
			mSwitchDisplay->SetIndicatorThreadSafe(false, mTempoPatch, 75);
		}
		return;
	case 2:
		ReceiveParamValue(&bytes[6], len - 6);
		return;
	case 0xf:
		ReceivePresetName(&bytes[6], len - 6);
		return;
	case 0xe:
		ReceivePresetEffects(&bytes[6], len - 6);
		return;
	case 8:
		if (mCheckedFirmware)
			return;
		ReceiveFirmwareVersionResponse(bytes, len);
		return;
	case 4:
		// skip byte 6 - may as well update state whenever we get it
//		StartReceivePatchDump(&bytes[7], len - 7);
		return;
	default:
		if (0 && mTrace)
		{
			const std::string byteDump(::GetAsciiHexStr(&bytes[5], len - 5, true));
			std::strstream traceMsg;
			traceMsg << byteDump.c_str() << std::endl << std::ends;
			mTrace->Trace(std::string(traceMsg.str()));
		}
	}
}

AxeEffectBlockInfo *
AxeFxManager::IdentifyBlockInfoUsingBypassId(const byte * bytes)
{
	const int effectIdLs = bytes[0];
	const int effectIdMs = bytes[1];
	const int parameterIdLs = bytes[2];
	const int parameterIdMs = bytes[3];

	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		AxeEffectBlockInfo & cur = *it;
		if (cur.mSysexEffectIdLs == effectIdLs &&
			cur.mSysexEffectIdMs == effectIdMs &&
			cur.mSysexBypassParameterIdLs == parameterIdLs &&
			cur.mSysexBypassParameterIdMs == parameterIdMs)
			return &(*it);
	}

	return NULL;
}

AxeEffectBlockInfo *
AxeFxManager::IdentifyBlockInfoUsingCc(const byte * bytes)
{
	const int effectIdLs = bytes[0];
	const int effectIdMs = bytes[1];
	const int effectId = effectIdMs << 4 | effectIdLs;
	const int ccLs = bytes[2];
	const int ccMs = bytes[3];
	const int cc = ccMs << 4 | ccLs;

	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		AxeEffectBlockInfo & cur = *it;
		if (cur.mSysexEffectId == effectId && cur.mBypassCC == cc)
			return &(*it);
	}

	return NULL;
}

// This method is provided for last chance guess.  Only use if all other methods
// fail.  It can return incorrect information since many effects are available
// in quantity.  This will return the first effect Id match to the exclusion
// of instance id.  It is meant for Feedback Return which is a single instance
// effect that has no default cc for bypass.
AxeEffectBlockInfo *
AxeFxManager::IdentifyBlockInfoUsingEffectId(const byte * bytes)
{
	const int effectIdLs = bytes[0];
	const int effectIdMs = bytes[1];

	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		AxeEffectBlockInfo & cur = *it;
		if (cur.mSysexEffectIdLs == effectIdLs &&
			cur.mSysexEffectIdMs == effectIdMs)
			return &(*it);
	}

	return NULL;
}

AxeEffectBlocks::iterator
AxeFxManager::GetBlockInfo(Patch * patch)
{
	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		const AxeEffectBlockInfo & cur = *it;
		if (cur.mPatch == patch)
			return it;
	}

	return mAxeEffectInfo.end();
}

void
AxeFxManager::ReceiveParamValue(const byte * bytes, int len)
{
/*
	0xdd effect ID LS nibble 
	0xdd effect ID MS nibble 
	0xdd parameter ID LS nibble 
	0xdd parameter ID MS nibble 
	0xdd parameter value LS nibble 
	0xdd parameter value MS nibble 
	0xdd null-terminated string byte0 
	0xdd byte1 
	... 
	0x00 null character 
	0xF7 sysex end 
 */

	KillResponseTimer();

	if (len < 8)
	{
		if (mTrace)
		{
			const std::string msg("truncated param value msg\n");
			mTrace->Trace(msg);
		}

		QMutexLocker lock(&mQueryLock);
		if (mQueries.begin() != mQueries.end())
			mQueries.pop_front();
	}
	else
	{
		AxeEffectBlockInfo * inf = NULL;

		{
			QMutexLocker lock(&mQueryLock);
			std::list<AxeEffectBlockInfo*>::iterator it = mQueries.begin();
			if (it != mQueries.end() &&
				(*it)->mSysexEffectIdLs == bytes[0] && 
				(*it)->mSysexEffectIdMs == bytes[1] &&
				(*it)->mSysexBypassParameterIdLs == bytes[2] &&
				(*it)->mSysexBypassParameterIdMs == bytes[3])
			{
				inf = *mQueries.begin();
				mQueries.pop_front();
			}
		}

		if (!inf)
		{
			// received something we weren't expecting; see if we can look it up
			inf = IdentifyBlockInfoUsingBypassId(bytes);
		}

		if (inf && inf->mPatch)
		{
			if (0 == bytes[5])
			{
				const bool isBypassed = (1 == bytes[4] || 3 == bytes[4]); // bypass param is 1 for bypassed, 0 for not bypassed (except for Amp and Pitch)
				const bool notBypassed = (0 == bytes[4] || 2 == bytes[4]);

				if (isBypassed || notBypassed)
				{
					if (inf->mPatch->IsActive() != notBypassed)
						inf->mPatch->UpdateState(mSwitchDisplay, notBypassed);

					if (0 && mTrace)
					{
						const std::string byteDump(::GetAsciiHexStr(bytes + 4, len - 6, true));
						const std::string asciiDump(::GetAsciiStr(&bytes[6], len - 8));
						std::strstream traceMsg;
						traceMsg << inf->mName << " : " << byteDump.c_str() << " : " << asciiDump.c_str() << std::endl << std::ends;
						mTrace->Trace(std::string(traceMsg.str()));
					}
				}
				else if (mTrace)
				{
					const std::string byteDump(::GetAsciiHexStr(bytes, len - 2, true));
					std::strstream traceMsg;
					traceMsg << "Unrecognized bypass param value for " << inf->mName << " " << byteDump.c_str() << std::endl << std::ends;
					mTrace->Trace(std::string(traceMsg.str()));
				}
			}
			else if (mTrace)
			{
				const std::string byteDump(::GetAsciiHexStr(bytes, len - 2, true));
				std::strstream traceMsg;
				traceMsg << "Unhandled bypass MS param value for " << inf->mName << " " << byteDump.c_str() << std::endl << std::ends;
				mTrace->Trace(std::string(traceMsg.str()));
			}
		}
		else
		{
			if (mTrace)
			{
				const std::string msg("Axe sync error: No inf or patch\n");
				mTrace->Trace(msg);
			}
		}
	}


	// post an event to kick off the next query
	class CreateSendNextQueryTimer : public QEvent
	{
		AxeFxManager *mMgr;

	public:
		CreateSendNextQueryTimer(AxeFxManager * mgr) : 
		  QEvent(User), 
		  mMgr(mgr)
		{
			mMgr->AddRef();
		}

		~CreateSendNextQueryTimer()
		{
			mMgr->RequestNextParamValue();
			mMgr->Release();
		}
	};

	QCoreApplication::postEvent(this, new CreateSendNextQueryTimer(this));
}

// consider: queue sync requests so that multiple patches on a single 
// switch result in a single request (handle on a timer)
void
AxeFxManager::SyncFromAxe()
{
	RequestPresetName();
}

void
AxeFxManager::SyncFromAxe(Patch * patch)
{
	// We have two methods to sync bypass state.
	// First is to use RequestPresetEffects.
	// Second is to use RequestParamValue.
	if (1)
	{
		// pro: if effect does not exist in preset, we'll know it by lack of return value 
		// pro: less chance of Axe-Fx deadlock?
		// pro or con: edit screen won't open to effect
		// alternatively: axe screen stays where it is at
		RequestPresetEffects();
		return;
	}

	// pro or con: RequestParamValue will cause the Axe to open the edit 
	//   screen of the effect being queried.
	// con: Potential for Axe-Fx deadlock?
	// con: If effect does not exist in preset, get a return value that can't
	//   be used to determine if it doesn't exist
	AxeEffectBlocks::iterator it = GetBlockInfo(patch);
	if (it == mAxeEffectInfo.end())
	{
		if (mTrace)
		{
			const std::string msg("Failed to locate AxeFx block info for AxeToggle patch\n");
			mTrace->Trace(msg);
		}
		return;
	}

	if (-1 == (*it).mSysexBypassParameterId)
		return;

	if (-1 == (*it).mSysexEffectId)
		return;

	if (!(*it).mPatch)
		return;

	if (!(*it).mEffectIsPresentInAxePatch)
	{
		// don't query if effect isn't present in current patch - to lessen
		// chance of axe-fx deadlock
		// opens us up to the possibility of getting a toggle out of sync with
		// any changes made by hand on the face of the unit or via other midi sources.
		// an alternative to consider would be to simply call RequestPresetEffects.
		return;
	}

	{
		QMutexLocker lock(&mQueryLock);
		mQueries.push_back(&(*it));
		if (mQueries.size() > 1)
			return; // should already be processing
	}

	// queue was empty, so kick off processing
	RequestNextParamValue();
}

void
AxeFxManager::KillResponseTimer()
{
	if (!mQueryTimer->isActive())
		return;

	class StopQueryTimer : public QEvent
	{
		AxeFxManager *mMgr;

	public:
		StopQueryTimer(AxeFxManager * mgr) : 
		  QEvent(User), 
		  mMgr(mgr)
		{
			mMgr->AddRef();
		}

		~StopQueryTimer()
		{
			mMgr->mQueryTimer->stop();
			mMgr->Release();
		}
	};

	QCoreApplication::postEvent(this, new StopQueryTimer(this));
}

void
AxeFxManager::QueryTimedOut()
{
	const clock_t curTime = ::clock();
	if (curTime > mLastTimeout + 5000)
		mTimeoutCnt = 0;
	mLastTimeout = curTime;
	++mTimeoutCnt;

	if (mTrace)
	{
		std::string msg;
		if (mTimeoutCnt > 3)
			msg = ("Multiple sync request timeouts, make sure it hasn't locked up\n");
		else
			msg = ("Axe sync request timed out\n");

		mTrace->Trace(msg);
	}

	if (mTimeoutCnt > 5)
	{
		mTimeoutCnt = 0;
		if (mQueries.size())
		{
			QMutexLocker lock(&mQueryLock);
			mQueries.clear();

			if (mTrace)
			{
				std::string msg("Aborting axe sync\n");
				mTrace->Trace(msg);
			}
		}
	}

	{
		QMutexLocker lock(&mQueryLock);
		if (mQueries.begin() != mQueries.end())
			mQueries.pop_front();
		else
			return;
	}

	RequestNextParamValue();
}

class StartQueryTimer : public QEvent
{
	AxeFxManager *mMgr;

public:
	StartQueryTimer(AxeFxManager * mgr) : 
	  QEvent(User), 
	  mMgr(mgr)
	{
		mMgr->AddRef();
	}

	~StartQueryTimer()
	{
		mMgr->mQueryTimer->start();
		mMgr->Release();
	}
};

void
AxeFxManager::DelayedSyncFromAxe()
{
	if (!mDelayedSyncTimer)
		return;

	if (mDelayedSyncTimer->isActive())
	{
		class StopDelayedSyncTimer : public QEvent
		{
			AxeFxManager *mMgr;

		public:
			StopDelayedSyncTimer(AxeFxManager * mgr) : 
			  QEvent(User), 
				  mMgr(mgr)
			{
				mMgr->AddRef();
			}

			~StopDelayedSyncTimer()
			{
				mMgr->mDelayedSyncTimer->stop();
				mMgr->Release();
			}
		};

		QCoreApplication::postEvent(this, new StopDelayedSyncTimer(this));
	}

	class StartDelayedSyncTimer : public QEvent
	{
		AxeFxManager *mMgr;

	public:
		StartDelayedSyncTimer(AxeFxManager * mgr) : 
		  QEvent(User), 
		  mMgr(mgr)
		{
			mMgr->AddRef();
		}

		~StartDelayedSyncTimer()
		{
			mMgr->mDelayedSyncTimer->start();
			mMgr->Release();
		}
	};

	QCoreApplication::postEvent(this, new StartDelayedSyncTimer(this));
}

void
AxeFxManager::RequestNextParamValue()
{
	if (!mMidiOut)
		return;

	if (!mCheckedFirmware)
		SendFirmwareVersionQuery();

	QMutexLocker lock(&mQueryLock);
	if (mQueries.begin() == mQueries.end())
		return;

	if (!mCheckedFirmware)
		return;

/* MIDI_SET_PARAMETER (or GET)
	0xF0 sysex start 
	0x00 Manf. ID byte0 
	0x01 Manf. ID byte1 
	0x74 Manf. ID byte2
	0x01 Model / device
	0x02 Function ID 
	0xdd effect ID LS nibble 
	0xdd effect ID MS nibble 
	0xdd parameter ID LS nibble 
	0xdd parameter ID MS nibble 
	0xdd parameter value LS nibble 
	0xdd parameter value MS nibble 
	0xdd query(0) or set(1) value 
	0xF7 sysex end 
*/

	const byte rawBytes[] = { 0xF0, 0x00, 0x01, 0x74, mModel, 0x02, 0, 0, 0, 0, 0x0, 0x0, 0x00, 0xF7 };
	Bytes bb(rawBytes, rawBytes + sizeof(rawBytes));
	AxeEffectBlockInfo * next = *mQueries.begin();
	bb[6] = next->mSysexEffectIdLs;
	bb[7] = next->mSysexEffectIdMs;
	bb[8] = next->mSysexBypassParameterIdLs;
	bb[9] = next->mSysexBypassParameterIdMs;

	QCoreApplication::postEvent(this, new StartQueryTimer(this));

	mMidiOut->MidiOut(bb);
}

void
AxeFxManager::SendFirmwareVersionQuery()
{
	// request firmware vers : F0 00 01 74 01 08 00 00 F7
	if (!mMidiOut)
		return;

	QMutexLocker lock(&mQueryLock);
	const byte rawBytes[] = { 0xF0, 0x00, 0x01, 0x74, 0, 0x08, 0, 0, 0xF7 };
	const Bytes bb(rawBytes, rawBytes + sizeof(rawBytes));
	mMidiOut->MidiOut(bb);
}

void
AxeFxManager::StartReceivePatchDump(const byte * bytes, int len)
{
	int idx;
	// header that is not passed into this method:
	// F0 00 01 74 01 04 01
	mPatchDumpBytesReceived = len + 7;

	// undetermined 6 bytes
	// 00 00 05 03 00 00 
// 	if (mTrace)
// 	{
// 		const std::string byteDump(::GetAsciiHexStr(&bytes[7], 6, true) + "\n");
// 		mTrace->Trace(byteDump);
// 	}

	// patch name 21 * 2bytes = 42 bytes
	// 05 04 0D 06 00 07 04 07 09 07 00 02 03 05 09 06 
	// 0C 06 05 06 0E 06 03 06 05 06 00 02 00 02 00 02 
	// 00 02 00 02 00 02 00 02 00 00 
	const int kNameBufLen = 42;
	std::string patchName;
	for (idx = 6; (idx + 2) < len && (idx + 2 - 6) <= kNameBufLen; idx += 2)
	{
		const byte ls = bytes[idx];
		const byte ms = bytes[idx + 1];
		const int ch = ms << 4 | ls;
		patchName += ch;
	}

	if (mMainDisplay && patchName.size())
	{
		patchName = "Axe-Fx " + patchName;
		mMainDisplay->AppendText(patchName);
	}
	
	// undetermined 22 bytes
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00

	// start of grid layout and effect ids
	// 4 bytes per block in 4 x 12 grid starting at bytes[70]:
	// 0A 06 00 00 0B 06 00 00 0C 06 00 00 0D 06 00 00 
	// 04 07 00 00 05 07 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	// 00 00 00 00 00 00 00 00 0C 07 00 00 0D 07 00 00

	// record all effect blocks
	mEditBufferEffectBlocks.clear();
	const int kBlockCount = 4 * 12;
	const int kBlockSize = 4 * kBlockCount;
	const int kStopByte = kBlockSize + 70 + 1; // stop byte is one beyond
	idx = 70;
	for (int blockIdx = 0; 
		(idx + 4) < len && (idx + 4) < kStopByte && blockIdx <= kBlockCount; 
		idx += 4, blockIdx++)
	{
		if (0 && mTrace)
		{
			const std::string byteDump(::GetAsciiHexStr(&bytes[idx], 4, true) + "\n");
			mTrace->Trace(byteDump);
		}

		const byte ls = bytes[idx];
		const byte ms = bytes[idx + 1];
		if (!ms && !ls)
			continue;

		const int effectId = ms << 4 | ls;
		mEditBufferEffectBlocks.insert(effectId);
	}
}

void
AxeFxManager::ContinueReceivePatchDump(const byte * bytes, int len)
{
	mPatchDumpBytesReceived += len;
	if (bytes[len - 1] == 0xf7)
	{
		// patch dump is 2060 bytes
		mPatchDumpBytesReceived = 0;
	}
}

void
AxeFxManager::RequestEditBufferDump()
{
	if (!mCheckedFirmware)
		SendFirmwareVersionQuery();

	KillResponseTimer();
	QMutexLocker lock(&mQueryLock);
	if (!mCheckedFirmware)
		return;
	mQueries.clear();
	const byte rawBytes[] = { 0xF0, 0x00, 0x01, 0x74, mModel, 0x03, 0x01, 0x00, 0x00, 0xF7 };
	const Bytes bb(rawBytes, rawBytes + sizeof(rawBytes));
	mPatchDumpBytesReceived = 0;
	mMidiOut->MidiOut(bb);
}

void
AxeFxManager::ReceiveFirmwareVersionResponse(const byte * bytes, int len)
{
	// response to firmware version request: F0 00 01 74 01 08 0A 03 F7
	std::string model;
	switch (bytes[4])
	{
	case 0:
		model = "Standard ";
		break;
	case 1:
		model = "Ultra ";
		mModel = 1;
		break;
	default:
		model = "Unknown model ";
	}

	if (mTrace)
	{
		std::strstream traceMsg;
		if (len >= 8)
			traceMsg << "Axe-Fx " << model << "version " << (int) bytes[6] << "." << (int) bytes[7] << std::endl << std::ends;
		else
			traceMsg << "Axe-Fx " << model << "version " << (int) bytes[6] << "." << (int) bytes[7] << std::endl << std::ends;
		mTrace->Trace(std::string(traceMsg.str()));
	}

	mCheckedFirmware = true;
}

void
AxeFxManager::RequestPresetName()
{
	if (!mCheckedFirmware)
		SendFirmwareVersionQuery();

	KillResponseTimer();
	QMutexLocker lock(&mQueryLock);
	if (!mCheckedFirmware)
		return;

	const byte rawBytes[] = { 0xF0, 0x00, 0x01, 0x74, mModel, 0x0f, 0xF7 };
	const Bytes bb(rawBytes, rawBytes + sizeof(rawBytes));
	mMidiOut->MidiOut(bb);
}

void
AxeFxManager::ReceivePresetName(const byte * bytes, int len)
{
	if (len < 21)
		return;

	std::string patchName((const char *)bytes, 21);
	if (mMainDisplay && patchName.size())
	{
		patchName = "Axe-Fx " + patchName;
		mMainDisplay->AppendText(patchName);
	}

	RequestPresetEffects();
}

void
AxeFxManager::RequestPresetEffects()
{
	if (!mCheckedFirmware)
		SendFirmwareVersionQuery();

	KillResponseTimer();
	QMutexLocker lock(&mQueryLock);
	if (!mCheckedFirmware)
		return;

	// default to no fx in patch, but don't update LEDs until later
	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		AxeEffectBlockInfo * cur = &(*it);
		if (-1 == cur->mSysexBypassParameterId)
			continue;

		if (-1 == cur->mSysexEffectId)
			continue;

		if (!cur->mPatch)
			continue;

		cur->mEffectIsPresentInAxePatch = false;
	}

	const byte rawBytes[] = { 0xF0, 0x00, 0x01, 0x74, mModel, 0x0e, 0xF7 };
	const Bytes bb(rawBytes, rawBytes + sizeof(rawBytes));
	mMidiOut->MidiOut(bb);
}

void
AxeFxManager::ReceivePresetEffects(const byte * bytes, int len)
{
	// for each effect, there are 2 bytes for the ID, 2 bytes for the bypass CC and 1 byte for the state
	// 0A 06 05 02 00
	for (int idx = 0; (idx + 5) < len; idx += 5)
	{
		if (0 && mTrace)
		{
			const std::string byteDump(::GetAsciiHexStr(&bytes[idx], 5, true) + "\n");
			mTrace->Trace(byteDump);
		}

		AxeEffectBlockInfo * inf = NULL;
		inf = IdentifyBlockInfoUsingCc(bytes + idx);
		if (!inf)
		{
			inf = IdentifyBlockInfoUsingEffectId(bytes + idx);
			if (inf && mTrace && inf->mNormalizedName != "feedback return")
			{
				std::strstream traceMsg;
				traceMsg << "Axe sync warning: potentially unexpected sync for  " << inf->mName << " " << std::endl << std::ends;
				mTrace->Trace(std::string(traceMsg.str()));
			}
		}

		if (inf && inf->mPatch)
		{
			inf->mEffectIsPresentInAxePatch = true;
			const byte kParamVal = bytes[idx + 4];
			const bool notBypassed = (1 == kParamVal);
			const bool isBypassed = (0 == kParamVal);

			if (isBypassed || notBypassed)
			{
				if (inf->mPatch->IsActive() != notBypassed)
					inf->mPatch->UpdateState(mSwitchDisplay, notBypassed);
			}
			else if (mTrace)
			{
				const std::string byteDump(::GetAsciiHexStr(bytes + idx, 5, true));
				std::strstream traceMsg;
				traceMsg << "Unrecognized bypass param value for " << inf->mName << " " << byteDump.c_str() << std::endl << std::ends;
				mTrace->Trace(std::string(traceMsg.str()));
			}
		}
		else
		{
			if (mTrace && !inf)
			{
				const std::string msg("Axe sync error: No inf\n");
				mTrace->Trace(msg);
			}
		}
	}

	// turn off LEDs for all effects not in cur preset
	for (AxeEffectBlocks::iterator it = mAxeEffectInfo.begin(); 
		it != mAxeEffectInfo.end(); 
		++it)
	{
		AxeEffectBlockInfo * cur = &(*it);
		if (-1 == cur->mSysexBypassParameterId)
			continue;

		if (-1 == cur->mSysexEffectId)
			continue;

		if (!cur->mPatch)
			continue;

		if (!cur->mEffectIsPresentInAxePatch)
			cur->mPatch->UpdateState(mSwitchDisplay, false);
	}
}

void 
NormalizeAxeEffectName(std::string &effectName) 
{
	std::transform(effectName.begin(), effectName.end(), effectName.begin(), ::tolower);

	int pos = effectName.find("axe");
	if (-1 != pos)
	{
		std::string searchStr;

		searchStr = "axe ";
		pos = effectName.find(searchStr);
		if (-1 == pos)
		{
			searchStr = "axefx ";
			pos = effectName.find(searchStr);
		}
		if (-1 == pos)
		{
			searchStr = "axe-fx ";
			pos = effectName.find(searchStr);
		}

		if (-1 != pos)
			effectName = effectName.substr(pos + searchStr.length());
	}

// 	pos = effectName.find(" mute");
// 	if (-1 != pos)
// 		effectName.replace(pos, 5, "");
// 	else
// 	{
// 		pos = effectName.find("mute ");
// 		if (-1 != pos)
// 			effectName.replace(pos, 5, "");
// 	}
// 
// 	pos = effectName.find(" toggle");
// 	if (-1 != pos)
// 		effectName.replace(pos, 7, "");
// 	else
// 	{
// 		pos = effectName.find("toggle ");
// 		if (-1 != pos)
// 			effectName.replace(pos, 7, "");
// 	}
// 
// 	pos = effectName.find(" momentary");
// 	if (-1 != pos)
// 		effectName.replace(pos, 10, "");
// 	else
// 	{
// 		pos = effectName.find("momentary ");
// 		if (-1 != pos)
// 			effectName.replace(pos, 10, "");
// 	}

	SynonymNormalization(effectName);
}

struct DefaultAxeCcs
{
	std::string mParameter;
	int			mCc;
};

// consider: define this list in an external xml file
DefaultAxeCcs kDefaultAxeCcs[] = 
{
	// these don't have defaults (some can't be bypassed)
	{"feedback send", 0},
	{"mixer 1", 0},
	{"mixer 2", 0},
	{"feedback return", 0},
	{"noisegate", 0},

	{"input volume", 10},
	{"out 1 volume", 11},
	{"out 2 volume", 12},
	{"output", 13},
	{"taptempo", 14},
	{"tuner", 15},
	{"external 1", 16},
	{"external 2", 17},
	{"external 3", 18},
	{"external 4", 19},
	{"external 5", 20},
	{"external 6", 21},
	{"external 7", 22},
	{"external 8", 23},

	{"looper 1 record", 24},
	{"looper 1 play", 25},
	{"looper 1 once", 26},
	{"looper 1 overdub", 27},
	{"looper 1 reverse", 28},
	{"looper 2 record", 29},
	{"looper 2 play", 30},
	{"looper 2 once", 31},
	{"looper 2 overdub", 32},
	{"looper 2 reverse", 33},

	{"global preset effect toggle", 34},
	{"volume increment", 35},
	{"volume decrement", 36},

	{"amp 1", 37},
	{"amp 2", 38},
	{"cabinet 1", 39},
	{"cabinet 2", 40},
	{"chorus 1", 41},
	{"chorus 2", 42},
	{"compressor 1", 43},
	{"compressor 2", 44},
	{"crossover 1", 45},
	{"crossover 2", 46},
	{"delay 1", 47},
	{"delay 2", 48},
	{"drive 1", 49},
	{"drive 2", 50},
	{"enhancer", 51},
	{"filter 1", 52},
	{"filter 2", 53},
	{"filter 3", 54},
	{"filter 4", 55},
	{"flanger 1", 56},
	{"flanger 2", 57},
	{"formant", 58},
	{"effects loop", 59},
	{"gate/expander 1", 60},
	{"gate/expander 2", 61},
	{"graphic eq 1", 62},
	{"graphic eq 2", 63},
	{"graphic eq 3", 64},
	{"graphic eq 4", 65},
	{"megatap delay", 66},
	{"multiband comp 1", 67},
	{"multiband comp 2", 68},
	{"multidelay 1", 69},
	{"multidelay 2", 70},
	{"parametric eq 1", 71},
	{"parametric eq 2", 72},
	{"parametric eq 3", 73},
	{"parametric eq 4", 74},
	{"phaser 1", 75},
	{"phaser 2", 76},
	{"pitch 1", 77},
	{"pitch 2", 78},
	{"quad chorus 1", 79},
	{"quad chorus 2", 80},
	{"resonator 1", 81},
	{"resonator 2", 82},
	{"reverb 1", 83},
	{"reverb 2", 84},
	{"ring mod", 85},
	{"rotary 1", 86},
	{"rotary 2", 87},
	{"synth 1", 88},
	{"synth 2", 89},
	{"panner/tremolo 1", 90},
	{"panner/tremolo 2", 91},
	{"vocoder", 92},
	{"vol/pan 1", 93},
	{"vol/pan 2", 94},
	{"vol/pan 3", 95},
	{"vol/pan 4", 96},
	{"wah-wah 1", 97},
	{"wah-wah 2", 98},
	{"", -1}
};

int
GetDefaultAxeCc(const std::string &effectNameIn, ITraceDisplay * trc) 
{
	std::string effectName(effectNameIn);
	NormalizeAxeEffectName(effectName);

	for (int idx = 0; !kDefaultAxeCcs[idx].mParameter.empty(); ++idx)
		if (kDefaultAxeCcs[idx].mParameter == effectName)
			return kDefaultAxeCcs[idx].mCc;

	if (trc)
	{
		std::string msg("ERROR: AxeFx default CC for " + effectName + " is unknown\n");
		trc->Trace(msg);
	}
	return 0;
}

void
SynonymNormalization(std::string & name)
{
#define MapName(subst, legit) if (name == subst) { name = legit; return; }

	int pos = name.find('(');
	if (std::string::npos != pos)
	{
		name = name.substr(0, pos);
		while (name.at(name.length() - 1) == ' ')
			name = name.substr(0, name.length() - 1);
	}

	switch (name[0])
	{
	case 'a':
		MapName("arpeggiator 1", "pitch 1");
		MapName("arpeggiator 2", "pitch 2");
		break;
	case 'b':
		MapName("band delay 1", "multidelay 1");
		MapName("band delay 2", "multidelay 2");
		MapName("band dly 1", "multidelay 1");
		MapName("band dly 2", "multidelay 2");
		break;
	case 'c':
		MapName("cab 1", "cabinet 1");
		MapName("cab 2", "cabinet 2");
		MapName("comp 1", "compressor 1");
		MapName("comp 2", "compressor 2");
		MapName("crystals 1", "pitch 1");
		MapName("crystals 2", "pitch 2");
		break;
	case 'd':
		MapName("delay 1 (reverse)", "delay 1");
		MapName("delay 2 (reverse)", "delay 2");
		MapName("detune 1", "pitch 1");
		MapName("detune 2", "pitch 2");
		MapName("device complete bypass toggle", "output");
		MapName("diff 1", "multidelay 1");
		MapName("diff 2", "multidelay 2");
		MapName("diffuser 1", "multidelay 1");
		MapName("diffuser 2", "multidelay 2");
		MapName("dly 1", "delay 1");
		MapName("dly 2", "delay 2");
		break;
	case 'e':
		MapName("ext 1", "external 1");
		MapName("ext 2", "external 2");
		MapName("ext 3", "external 3");
		MapName("ext 4", "external 4");
		MapName("ext 5", "external 5");
		MapName("ext 6", "external 6");
		MapName("ext 7", "external 7");
		MapName("ext 8", "external 8");
		MapName("extern 1", "external 1");
		MapName("extern 2", "external 2");
		MapName("extern 3", "external 3");
		MapName("extern 4", "external 4");
		MapName("extern 5", "external 5");
		MapName("extern 6", "external 6");
		MapName("extern 7", "external 7");
		MapName("extern 8", "external 8");
		break;
	case 'f':
		MapName("fdbk ret", "feedback return");
		MapName("fdbk rtrn", "feedback return");
		MapName("fdbk return", "feedback return");
		MapName("feedback ret", "feedback return");
		MapName("feedback rtrn", "feedback return");
		MapName("fx loop", "effects loop");
		break;
	case 'g':
		MapName("gate 1", "gate/expander 1");
		MapName("gate 2", "gate/expander 2");
		MapName("graphiceq 1", "graphic eq 1");
		MapName("graphiceq 2", "graphic eq 2");
		MapName("graphiceq 3", "graphic eq 3");
		MapName("graphiceq 4", "graphic eq 4");
		break;
	case 'i':
		MapName("in vol", "input volume");
		MapName("input", "input volume");
		MapName("input vol", "input volume");
		break;
	case 'l':
		MapName("loop 1 rec", "looper 1 record");
		MapName("loop 1 record", "looper 1 record");
		MapName("loop 1 play", "looper 1 play");
		MapName("loop 1 once", "looper 1 once");
		MapName("loop 1 overdub", "looper 1 overdub");
		MapName("loop 1 rev", "looper 1 reverse");
		MapName("loop 1 reverse", "looper 1 reverse");
		MapName("loop 2 rec", "looper 2 record");
		MapName("loop 2 record", "looper 2 record");
		MapName("loop 2 play", "looper 2 play");
		MapName("loop 2 once", "looper 2 once");
		MapName("loop 2 overdub", "looper 2 overdub");
		MapName("loop 2 rev", "looper 2 reverse");
		MapName("loop 2 reverse", "looper 2 reverse");
		MapName("looper 1", "delay 1");
		MapName("looper 2", "delay 2");
		break;
	case 'm':
		MapName("mdly 1", "multidelay 1");
		MapName("mdly 2", "multidelay 2");
		MapName("megatap", "megatap delay");
		MapName("mix 1", "mixer 1");
		MapName("mix 2", "mixer 2");
		MapName("multi comp 1", "multiband comp 1");
		MapName("multi comp 2", "multiband comp 2");
		MapName("multi delay 1", "multidelay 1");
		MapName("multi delay 2", "multidelay 2");
		break;
	case 'n':
		MapName("noise gate", "noisegate");
		break;
	case 'o':
		MapName("octave 1", "pitch 1");
		MapName("octave 2", "pitch 2");
		MapName("out", "output");
		MapName("out 1 vol", "out 1 volume");
		MapName("out 2 vol", "out 2 volume");
		MapName("output 1 vol", "out 1 volume");
		MapName("output 2 vol", "out 2 volume");
		break;
	case 'p':
		MapName("pan/trem 1", "panner/tremolo 1");
		MapName("pan/trem 2", "panner/tremolo 2");
		MapName("para eq 1", "parametric eq 1");
		MapName("para eq 2", "parametric eq 2");
		MapName("para eq 3", "parametric eq 3");
		MapName("para eq 4", "parametric eq 4");
		MapName("pitch 1 (whammy)", "pitch 1");
		MapName("pitch 2 (whammy)", "pitch 2");
		MapName("plex 1", "multidelay 1");
		MapName("plex 2", "multidelay 2");
		MapName("plex delay 1", "multidelay 1");
		MapName("plex delay 2", "multidelay 2");
		MapName("plex detune 1", "multidelay 1");
		MapName("plex detune 2", "multidelay 2");
		MapName("plex dly 1", "multidelay 1");
		MapName("plex dly 2", "multidelay 2");
		MapName("plex shift 1", "multidelay 1");
		MapName("plex shift 2", "multidelay 2");
		break;
	case 'q':
		MapName("quad 1", "quad chorus 1");
		MapName("quad 2", "quad chorus 2");
		MapName("quad delay 1", "multidelay 1");
		MapName("quad delay 2", "multidelay 2");
		MapName("quad dly 1", "multidelay 1");
		MapName("quad dly 2", "multidelay 2");
		MapName("quad series 1", "multidelay 1");
		MapName("quad series 2", "multidelay 2");
		MapName("quad tap 1", "multidelay 1");
		MapName("quad tap 2", "multidelay 2");
		MapName("quadchorus 1", "quad chorus 1");
		MapName("quadchorus 2", "quad chorus 2");
		break;
	case 'r':
		MapName("return", "feedback return");
		MapName("reverse 1", "delay 1");
		MapName("reverse 2", "delay 2");
		MapName("reverse delay 1", "delay 1");
		MapName("reverse delay 2", "delay 2");
		MapName("reverse dly 1", "delay 1");
		MapName("reverse dly 2", "delay 2");
		MapName("rhythm tap 1", "multidelay 1");
		MapName("rhythm tap 2", "multidelay 2");
		MapName("ring modulator", "ring mod");
		MapName("ringmod", "ring mod");
		break;
	case 's':
		MapName("send", "feedback send");
		MapName("shift 1", "pitch 1");
		MapName("shift 2", "pitch 2");
		break;
	case 't':
		MapName("tap", "taptempo");
		MapName("tap tempo", "taptempo");
		MapName("ten tap 1", "multidelay 1");
		MapName("ten tap 2", "multidelay 2");
		MapName("trem 1", "panner/tremolo 1");
		MapName("trem 2", "panner/tremolo 2");
		MapName("tremolo 1", "panner/tremolo 1");
		MapName("tremolo 2", "panner/tremolo 2");
		break;
	case 'v':
		MapName("vol dec", "volume decrement");
		MapName("vol decrement", "volume decrement");
		MapName("vol inc", "volume increment");
		MapName("vol increment", "volume increment");
		MapName("vol 1", "vol/pan 1");
		MapName("vol 2", "vol/pan 2");
		MapName("vol 3", "vol/pan 3");
		MapName("vol 4", "vol/pan 4");
		MapName("volume 1", "vol/pan 1");
		MapName("volume 2", "vol/pan 2");
		MapName("volume 3", "vol/pan 3");
		MapName("volume 4", "vol/pan 4");
		break;
	case 'w':
		MapName("wah 1", "wah-wah 1");
		MapName("wah 2", "wah-wah 2");
		MapName("wahwah 1", "wah-wah 1");
		MapName("wahwah 2", "wah-wah 2");
		MapName("whammy 1", "pitch 1");
		MapName("whammy 2", "pitch 2");
		break;
	case 'x':
		MapName("x-over 1", "crossover 1");
		MapName("x-over 2", "crossover 2");
		break;
	}
}
