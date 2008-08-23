/*
Original code copyright (c) 2007-2008 Sean Echevarria ( http://www.creepingfog.com/sean/ )

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#ifndef SEHexception_h__
#define SEHexception_h__

#include <Windows.h>
#include <WinNT.h>


struct SEHexception
{
    SEHexception() { }
    SEHexception(unsigned int n) : mSEnum(n) { }
    unsigned int mSEnum;
};

inline void trans_func(unsigned int /*u*/, EXCEPTION_POINTERS* /*pExp*/)
{
    throw SEHexception();
}

#endif // SEHexception_h__
