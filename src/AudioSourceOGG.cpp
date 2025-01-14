#include "pch.h"

#include "Audio.h"
#include "Audiofile.h"
#include "AudioSourceOGG.h"
#include "Logging.h"

/*
    you may ask, "why do you use libogg directly instead of libsndfile?"
    and I can only respond - ogg on libsndfile is broken as hell.
*/

size_t readOGG(void* ptr, size_t size, size_t nmemb, void* p)
{
    FILE* fp = (FILE*)p;
    return fread(ptr, size, nmemb, fp);
}

int seekOGG(void* p, ogg_int64_t offs, int whence)
{
    FILE* fp = (FILE*)p;
    return fseek(fp, offs, whence);
}

long tellOGG(void* p)
{
    FILE* fp = (FILE*)p;
    return ftell(fp);
}

int closeOgg(void* p)
{
    return fclose((FILE*)p);
}

ov_callbacks fileInterfaceOgg = {
    readOGG,
    seekOGG,
    closeOgg,
    tellOGG
};

AudioSourceOGG::AudioSourceOGG()
{
    mIsValid = false;
    mSourceLoop = false;
    mIsDataLeft = false;
    mSeekTime = -1;
}

AudioSourceOGG::~AudioSourceOGG()
{
    if (mIsValid)
        ov_clear(&mOggFile);
}

bool AudioSourceOGG::Open(const char* Filename)
{
#if !(defined WIN32) || (defined MINGW)
    int32 retv = ov_fopen(Filename, &mOggFile);
#else
    FILE* fp = _wfopen(Utility::Widen(Filename).c_str(), L"rb");
    int retv = -1;

    if (fp)
        retv = ov_open_callbacks(static_cast<void*>(fp), &mOggFile, nullptr, 0, fileInterfaceOgg);
#endif

#ifndef NDEBUG
    dFILENAME = Filename;
#endif

#if !(defined WIN32) || (defined MINGW)
    if (retv == 0)
#else
    if (retv == 0 && fp)
#endif
    {
        info = ov_info(&mOggFile, -1);
        comment = ov_comment(&mOggFile, -1);

        mIsValid = true;
        mIsDataLeft = true;
    }
    else
    {
        mIsValid = false;
        Log::Printf("Failure loading ogg file: %s (%d)\n", Filename, retv);
    }

    return mIsValid;
}

uint32_t AudioSourceOGG::Read(short* buffer, size_t count)
{
    size_t size;
    size_t read = 0;
    int sect;

    if (!mIsValid)
        return 0;

    size = count*sizeof(short);

    if (mSeekTime >= 0)
    {
        mIsDataLeft = true;
        ov_time_seek(&mOggFile, mSeekTime);
        mSeekTime = -1;
    }

    /* read from ogg vorbis file */
    size_t res = 1;
    while (read < size)
    {
        res = ov_read(&mOggFile, (char*)buffer + read, size - read, 0, 2, 1, &sect);

        if (res > 0)
            read += res;

        else if (res == 0)
        {
            if (mSourceLoop)
            {
                ov_time_seek(&mOggFile, 0);
            }
            else
            {
                mIsDataLeft = false;
                return 0;
            }
        }
        else
        {
            Log::Printf("AudioSourceOGG: Error while reading OGG source (%d)\n", res);
            mIsDataLeft = false;
            return 0;
        }
    }

    return read / sizeof(short);
}

void AudioSourceOGG::Seek(float Time)
{
    mSeekTime = Time;
}

size_t AudioSourceOGG::GetLength()
{
    return ov_pcm_total(&mOggFile, -1);
}

uint32_t AudioSourceOGG::GetRate()
{
    return info->rate;
}

uint32_t AudioSourceOGG::GetChannels()
{
    return info->channels;
}

bool AudioSourceOGG::IsValid()
{
    return mIsValid;
}

bool AudioSourceOGG::HasDataLeft()
{
    return mIsDataLeft;
}

std::string GetOggTitle(std::string file)
{
    OggVorbis_File f;
    std::string result = "";
    if (ov_fopen(file.c_str(), &f) == 0)
    {
        vorbis_comment *comment = ov_comment(&f, -1);

        for (int i = 0; i < comment->comments; i++)
        {
            std::string user_comment = comment->user_comments[i];
            auto splitvec = Utility::TokenSplit(user_comment, "=");
            if (splitvec[0] == "TITLE")
            {
                result = splitvec[1];
                break;
            }
        }

        ov_clear(&f);
    }

    return result;
}