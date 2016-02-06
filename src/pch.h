// Need to use this instead of #pragma once to be able to build on Mac OS X.
#ifndef RAINDROP_PCH_H
#define RAINDROP_PCH_H

// It is not possible to disable pch for C file, so I need to do this.
#ifdef __cplusplus

#if (defined _MSC_VER) && (_MSC_VER < 1800)
#error "You require Visual Studio 2013 or higher to compile this application."
#endif

#ifdef WIN32
#pragma warning (disable: 4244) // possible loss of data
#pragma warning (disable: 4996) // deprecation
#pragma warning (disable: 4800) // cast from bool to int

#define _USE_MATH_DEFINES

#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#include <pa_win_wasapi.h>
#include <pa_win_ds.h>

#ifndef MINGW
#include <direct.h>
#include <mpg123.h>
#else
#include <mpg123mingw.h>
#endif

#else
#include <iconv.h>
#endif

#ifdef LINUX
#include <pa_linux_alsa.h>
#endif

#ifdef __APPLE__
#include <mpg123.h>
#endif

#if _MSC_VER >= 1900
#include <filesystem>
namespace std
{
    namespace filesystem = experimental::filesystem;
}
#else
#include <boost/filesystem.hpp>
namespace std
{
    namespace filesystem = boost::filesystem;
}
#endif

// STL
#include <algorithm>
#include <atomic>
#include <chrono>
#include <codecvt>
#include <condition_variable>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
// #include <randint> // C++17 example implementation???

// C stdlib
#include <cctype>
#include <clocale>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// boost
#include <boost/gil/extension/io/bmp_all.hpp>
#include <boost/gil/extension/io/png_all.hpp>
#include <boost/gil/extension/io/jpeg_all.hpp>
#include <boost/gil/extension/io/targa_all.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/program_options.hpp>

// librocket
#include <Rocket/Controls.h>
#include <Rocket/Core.h>
#include <Rocket/Debugger.h>
#include <Rocket/Core/Lua/Interpreter.h>
#include <Rocket/Controls/Lua/Controls.h>

// OpenGL
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <GLFW/glfw3.h>

// audio
#include <portaudio/portaudio.h>
#include <soxr.h>
#include <ogg/ogg.h>
#include <vorbis/vorbisfile.h>
#include <sndfile.h>

// others
#include <stb_truetype.h>
#include <sqlite3.h>
#include <LuaBridge.h>
#include <sys/stat.h>

// static libraries
#include "ext/json/json.h"
#include "ext/sha256.h"
#include "ext/pa_ringbuffer.h"
#include "ext/SimpleIni.h"
#include "ext/utf8.h"

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Mat4 = glm::mat4;

template
<class T>
struct TAABB
{
    union
    {
        struct { T X, Y; } P1;
        struct { T X1, Y1; }; // Topleft point
    };

    union
    {
        struct { T X, Y; } P2;
        struct { T X2, Y2; }; // Bottomright point
    };
};

using AABB = TAABB<float>;
using AABBd = TAABB<double>;

template
<class T>
struct TColorRGB
{
    union
    {
        struct { T R, G, B, A; };
        struct { T Red, Green, Blue, Alpha; };
    };
};

using ColorRGB = TColorRGB<float>;
using ColorRGBd = TColorRGB<double>;

namespace Color
{
    extern const ColorRGB White;
    extern const ColorRGB Black;
    extern const ColorRGB Red;
    extern const ColorRGB Green;
    extern const ColorRGB Blue;
}

template <class T>
struct Fraction
{
    T Num;
    T Den;

    Fraction()
    {
        Num = Den = 1;
    }

    template <class K>
    Fraction(K num, K den)
    {
        Num = num;
        Den = den;
    }

    void fromDouble(double in)
    {
        double d = 0;
        Num = 0;
        Den = 1;
        while (d != in)
        {
            if (d < in)	++Num;
            else if (d > in) ++Den;
            d = static_cast<double>(Num) / Den;
        }
    }
};

using LFraction = Fraction<long long>;
using IFraction = Fraction<int>;

namespace Utility
{
    void DebugBreak();
    bool IsNumeric(const char* s);
    std::string RelativeToPath(std::string Filename);
    std::string RemoveExtension(std::string Fn);
    bool FileExists(std::string Filename);
    std::wstring Widen(std::string Line);
    std::string Narrow(std::wstring Line);
    std::string SJIStoU8(std::string Line);
    void CheckDir(std::string Dirname);
    int GetLMT(std::string Path);
    std::string GetSha256ForFile(std::string Filename);
    std::string IntToStr(int num);
    std::string CharToStr(char c);
    void RemoveFilenameIllegalCharacters(std::string &S, bool removeSlash, bool noAbsolute = true);

    std::string Format(std::string str, ...);

    std::vector<std::string> TokenSplit(const std::string &str, const std::string &token = ",", bool compress = false);

    std::string Trim(std::string& str);
    std::string ReplaceAll(std::string& str, const std::string& seq, const std::string what);
    std::string ToLower(std::string& str); // Caveat: only for ascii purposes.

	std::vector<std::filesystem::path> GetFileListing(std::filesystem::path path);

    template <class T>
    std::string Join(const T& iterable, const std::string& seq)
    {
        std::string ret;
        for (auto s = iterable.begin(); s != iterable.end(); ++s)
        {
            auto next = s; ++next;
            if (next != iterable.end())
                ret += *s + seq;
            else
                ret += *s;
        }

        return ret;
    }
}

enum KeyEventType
{
    KE_NONE,
    KE_PRESS,
    KE_RELEASE
};

template <class T>
T abs(T x)
{
    return x > 0 ? x : -x;
}

inline bool IntervalsIntersect(const double a, const double b, const double c, const double d)
{
    return a <= d && c <= b;
}

template <class T>
inline T LerpRatio(const T &Start, const T& End, double Progress, double Total)
{
    return Start + (End - Start) * Progress / Total;
}

template <class T>
inline T Lerp(const T &Start, const T& End, double k)
{
    return Start + (End - Start) * k;
}

template <class T>
inline T Clamp(const T &Value, const T &Min, const T &Max)
{
    if (Value < Min) return Min;
    else if (Value > Max) return Max;
    else return Value;
}

template <class T>
inline T clamp_to_interval(const T& value, const T& target, const T& interval)
{
    T output = value;
    while (output > target + interval) output -= interval * 2;
    while (output < target - interval) output += interval * 2;
    return output;
}

int b36toi(const char* txt);
int b16toi(const char* txt);

int LCM(const std::vector<int> &Set);
double latof(std::string s);

#include "directory.h"

#endif
#endif
