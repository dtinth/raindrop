#ifdef WIN32
#include <Windows.h>
#include <direct.h>
#endif

#include "Global.h"
#include <cstring>
#include <csignal>
#include <sys/stat.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/classification.hpp>

int InfinityMask = 0x7F800000;
float *PInfinity = (float*)&InfinityMask;

namespace Utility {

	void DebugBreak()
	{
#ifndef NDEBUG
#ifdef WIN32
		__asm int 3
#else
		raise(SIGTRAP);
#endif
#endif
	}

#ifndef isdigit
	bool isdigit(char s)
	{
		return s >= '0' && s <= '9';
	}
#endif

	bool IsNumeric(const char* s)
	{
		// check for first character being a minus sign
		std::stringstream k;
		double d;
		k << s;
		k >> d;
		return !k.fail();
	}

	String GetExtension(String Filename)
	{
		return Filename.substr(Filename.find_last_of(".")+1);
	}

	String RelativeToPath(String Filename)
	{
		return Filename.substr(Filename.find_last_of("/"));
	}

	String RemoveExtension(String Fn)
	{
		return Fn.substr(0, Fn.find_last_of("."));
	}

	bool FileExists(String Fn)
	{
#ifndef WIN32
		struct stat st;
		return (stat(Fn.c_str(), &st) != -1);
#else
		return _waccess( Utility::Widen(Fn).c_str(), 00 ) != -1;
#endif
	}

	std::wstring Widen(String Line)
	{
		wchar_t u16s[2048]; // Ought to be enough for everyone.

#ifndef WIN32
		mbstowcs(u16s, Line.c_str(), 2048);
#else
		memset(u16s, 0, sizeof(wchar_t) * 2048);
		size_t len = MultiByteToWideChar(CP_UTF8, 0, Line.c_str(), Line.length(), u16s, 2048);
#endif
		return std::wstring(u16s);
	}

	String Narrow(std::wstring Line)
	{
		char mbs[2048];
		
#ifndef WIN32
		wcstombs(mbs, Line.c_str(), 2048);
#else
		memset(mbs, 0, 2048);
		size_t len = WideCharToMultiByte(CP_UTF8, 0, Line.c_str(), Line.length(), mbs, 2048, NULL, NULL);
#endif
		return String(mbs);
	}

	void CheckDir(String path)
	{
		struct stat st;
		if (stat(path.c_str(), &st))
		{
#ifdef WIN32
			_mkdir(path.c_str());
#else
			mkdir(path, S_IWUSR);
#endif
		}
	}

	int GetLMT(String Path)
	{
		struct stat st;
		if (stat(Path.c_str(), &st) != -1)
		{
			return st.st_mtime;
		}
		else return 0;
	}

	void RemoveFilenameIllegalCharacters(String &S, bool removeSlash)
	{
		// size_t len = strlen(fn);
		boost::replace_all(S, "<", "");
		boost::replace_all(S, ">", "");
		boost::replace_all(S, ":", "");
		boost::replace_all(S, "\"", "");
		boost::replace_all(S, "|", "");
		boost::replace_all(S, "?", "");
		boost::replace_all(S, "*", "");
		if (removeSlash)
			boost::replace_all(S, "/", "");
	}

} // namespace Utility

#ifdef NDEBUG
namespace boost
{
void throw_exception(std::exception const&)
{
}
}
#endif
