#include "pch.h"

#include "Logging.h"
#include "Audio.h"
#include "AudioSourceMP3.h"

static bool mpg123_initialized = false;

AudioSourceMP3::AudioSourceMP3()
{
    int err;

    if (!mpg123_initialized)
    {
        mpg123_init();
        mpg123_initialized = true;
    }

    mHandle = mpg123_new(nullptr, &err);
    mpg123_format_none(mHandle);
    mIsValid = false;
    mIsDataLeft = false;
}

AudioSourceMP3::~AudioSourceMP3()
{
    mpg123_close(mHandle);
    mpg123_delete(mHandle);
}

bool AudioSourceMP3::Open(const char* Filename)
{
    mpg123_param(mHandle, MPG123_FORCE_RATE, 44100, 1);

#ifndef NDEBUG
    dFILENAME = Filename;
#endif

#if !(defined WIN32) || (defined MINGW)
    if (mpg123_open(mHandle, Filename) == MPG123_OK)
#else
    if (mpg123_topen(mHandle, Utility::Widen(Filename).c_str()) == MPG123_OK)
#endif
    {
        long rate;

        mpg123_format_all(mHandle);
        mpg123_format(mHandle, 44100, MPG123_STEREO, MPG123_ENC_SIGNED_16);

        mpg123_getformat(mHandle, &rate, &mChannels, &mEncoding);

        mRate = rate;

        size_t pos = mpg123_tell(mHandle);
        size_t start = mpg123_seek(mHandle, 0, SEEK_SET);
        size_t end = mpg123_seek(mHandle, 0, SEEK_END);
        mpg123_seek(mHandle, pos, SEEK_SET);

        mLen = end - start;

        mIsValid = true;
        mIsDataLeft = true;
        return true;
    }

    return false;
}

uint32_t AudioSourceMP3::Read(short* buffer, size_t count)
{
    size_t actuallyread;
    auto toRead = count * sizeof(short); // # of bytes to actually read

    if (toRead == 0 || !IsValid())
        return 0;

    // read # bytes into varr.

    auto res = mpg123_read(mHandle, (unsigned char*)buffer, toRead, &actuallyread);
    size_t last_read = actuallyread;

    if (actuallyread == 0) return 0; // Huh.

    while (mSourceLoop && actuallyread < toRead)
    {
        if (res == MPG123_DONE)
            Seek(0);

        count -= last_read / sizeof(short);

        res = mpg123_read(mHandle, reinterpret_cast<unsigned char*>(buffer) + actuallyread, count, &last_read);
        actuallyread += last_read;
    }

    // according to mpg123_read documentation, ret is the amount of bytes read. We want to return samples read.
    return actuallyread / sizeof(short);
}

void AudioSourceMP3::Seek(float Time)
{
    mIsDataLeft = true;
    size_t place = mRate * Time;
    int res = mpg123_seek(mHandle, place, SEEK_SET);
    if (res < 0 || res < place)
    {
        Log::Printf("Error seeking stream at %f (tried %d, got %d)\n", Time, place, res);
    }

    if (place > mLen)
        Log::Printf("Attempt to seek after the stream's end.\n");
}

size_t AudioSourceMP3::GetLength()
{
    return mLen;
}

uint32_t AudioSourceMP3::GetRate()
{
    return mRate;
}

uint32_t AudioSourceMP3::GetChannels()
{
    return mChannels;
}

bool AudioSourceMP3::IsValid()
{
    if (!mIsValid) return false;

    auto err = mHandle ? mpg123_errcode(mHandle) : MPG123_OK;
    auto isValidMp3Stream = (err == MPG123_OK ||
        err == MPG123_DONE ||
        err == MPG123_ERR_READER); // This third one is because valid streams throw off this error.
    if (!isValidMp3Stream)
    {
        Log::Printf("Mp3 Decoder error: %s\n", mpg123_strerror(mHandle));
        mIsValid = false;
    }

    return mHandle && isValidMp3Stream;
}

bool AudioSourceMP3::HasDataLeft()
{
    return mIsDataLeft;
}