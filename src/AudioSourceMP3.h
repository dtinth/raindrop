#pragma once

#ifdef MP3_ENABLED

class AudioSourceMP3 : public AudioDataSource
{
    mpg123_handle *mHandle;
    uint32_t mRate;
    int mEncoding;
    int mChannels;
    size_t mLen;

    bool mIsValid;
    bool mIsDataLeft;

public:
    AudioSourceMP3();
    ~AudioSourceMP3();
    bool Open(const char* Filename) override;
    uint32_t Read(short* buffer, size_t count) override;
    void Seek(float Time) override;
    size_t GetLength() override; // Always returns total samples. Frames = Length/Channels.
    uint32_t GetRate() override; // Returns sampling rate of audio
    uint32_t GetChannels() override; // Returns channels of audio
    bool IsValid() override;
    bool HasDataLeft() override;
};

#endif