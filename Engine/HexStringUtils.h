#ifndef HexStringUtils_h__
#define HexStringUtils_h__

#include <string>
#include <vector>

typedef std::vector<byte> Bytes;


byte CharToByte(const char ch);
byte CharsToByte(const char ch1, const char ch2);
byte CharsToByte(const char[2] & chrs);
int ValidateString(std::string inString, Bytes & outBytes);
std::string GetAsciiHexStr(const Bytes & inBytes, bool format /*= true*/);

#endif // HexStringUtils_h__
