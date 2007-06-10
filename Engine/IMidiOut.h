#ifndef IMidiOut_h__
#define IMidiOut_h__

#include <string>
#include <vector>

typedef unsigned char byte;
typedef std::vector<byte> Bytes;


// IMidiOut
// ----------------------------------------------------------------------------
// use to send MIDI
//
class IMidiOut
{
public:
	virtual unsigned int GetMidiOutDeviceCount() = 0;
	virtual std::string GetMidiOutDeviceName(unsigned int deviceIdx) = 0;
	virtual bool OpenMidiOut(unsigned int deviceIdx) = 0;
	virtual bool MidiOut(const Bytes & bytes) = 0;
	virtual void CloseMidiOut() = 0;
};

#endif // IMidiOut_h__
