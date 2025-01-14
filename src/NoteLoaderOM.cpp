#include "pch.h"

#include "GameGlobal.h"
#include "Logging.h"
#include "Song7K.h"
#include "NoteLoader7K.h"

typedef std::vector<std::string> SplitResult;

using namespace VSRG;

#define SAMPLESET_NORMAL 1
#define SAMPLESET_SOFT 2
#define SAMPLESET_DRUM 3

#define HITSOUND_NORMAL 0
#define HITSOUND_WHISTLE 2
#define HITSOUND_FINISH 4
#define HITSOUND_CLAP 8

#define NOTE_SLIDER 2
#define NOTE_HOLD 128
#define NOTE_NORMAL 1

struct HitsoundSectionData
{
    int Sampleset;
    int Volume; // In %
    int Custom;
    int IsInherited;
    double Time; // In Seconds
    double Value; // BPM or Multiplier
    double MeasureLen; // In quarter notes
    // bool Kiai;
    bool Omit;
};

bool operator <(const HitsoundSectionData& lhs, const HitsoundSectionData& rhs)
{
    return lhs.Time < rhs.Time;
}

struct OsuLoadInfo
{
    double SliderVelocity;
    int Version;
    int last_sound_index;
    Song *OsuSong;
    std::shared_ptr<VSRG::OsuManiaTimingInfo> TimingInfo;
    std::map <std::string, int> Sounds;
    std::vector<HitsoundSectionData> HitsoundSections;
    std::shared_ptr<VSRG::Difficulty> Diff;
    std::string DefaultSampleset;

    bool ReadAModeTag;

    std::vector<NoteData> Notes[MAX_CHANNELS];
    int Line;

    double GetBeatspaceAt(double T)
    {
        double Ret;
        if (HitsoundSections.size())
        {
            auto Current = HitsoundSections.begin();
            while (Current != HitsoundSections.end() && (Current->Time > T || Current->IsInherited))
                ++Current;

            if (Current == HitsoundSections.end())
            {
                Current = HitsoundSections.begin();
                while (Current != HitsoundSections.end() && Current->IsInherited)
                    ++Current;

                if (Current == HitsoundSections.end())
                    throw std::runtime_error("No uninherited timing points were found!");
                else
                    Ret = Current->Value;
            }
            else
                Ret = Current->Value;
        }
        else
            throw std::runtime_error("No timing points found on this osu! file.");

        return Ret;
    }

    OsuLoadInfo()
    {
        ReadAModeTag = false;
        Line = 0;
    }

    float GetSliderMultiplierAt(double T)
    {
        double Ret = 1;
        if (HitsoundSections.size())
        {
            auto Current = HitsoundSections.begin();
            while (Current != HitsoundSections.end() && (Current->Time >= T || !Current->IsInherited))
                ++Current;

            // So, we're at a valid SV mult section, the time is correct
            if (Current == HitsoundSections.end())
                return 1;
            else
                Ret = Current->Value;
        }

        return Ret;
    }
};

/* osu!mania loader. credits to wanwan159, woc2006, Zorori and the author of AIBat for helping me understand this. */

bool ReadGeneral(std::string line, OsuLoadInfo* Info)
{
    std::string Command = line.substr(0, line.find_first_of(" ")); // Lines are Information:<space>Content
    std::string Content = line.substr(line.find_first_of(":") + 1);

    Content = Content.substr(Content.find_first_not_of(" "));

    if (Command == "AudioFilename:")
    {
        if (Content == "virtual")
        {
            Info->Diff->IsVirtual = true;
            return true;
        }
        else
        {
#ifdef VERBOSE_DEBUG
            printf("Audio filename found: %s\n", Content.c_str());
#endif
            Utility::Trim(Content);
            Info->OsuSong->SongFilename = Content;
            Info->OsuSong->SongPreviewSource = Content;
        }
    }
    else if (Command == "Mode:")
    {
        Info->ReadAModeTag = true;
        if (Content != "3") // It's not a osu!mania chart, so we can't use it.
            return false;
    }
    else if (Command == "SampleSet:")
    {
        Utility::ToLower(Content); Utility::Trim(Content);
        Info->DefaultSampleset = Content;
    }
    else if (Command == "PreviewTime:")
    {
        if (Content != "-1")
        {
            if (Info->OsuSong->PreviewTime == 0)
                Info->OsuSong->PreviewTime = latof(Content) / 1000;
        }
    }
    else if (Command == "SpecialStyle:")
    {
        if (Content == "1")
            Info->Diff->Data->Turntable = true;
    }

    return true;
}

void ReadMetadata(std::string line, OsuLoadInfo* Info)
{
    auto Command = line.substr(0, line.find_first_of(":")); // Lines are Information:Content
    auto Content = line.substr(line.find_first_of(":") + 1, line.length() - line.find_first_of(":"));

#ifdef VERBOSE_DEBUG
    printf("Command found: %s | Contents: %s\n", Command.c_str(), Content.c_str());
#endif

    Utility::Trim(Content);
    if (Command == "Title")
    {
        Info->OsuSong->SongName = Content;
    }
    else if (Command == "Artist")
    {
        Info->OsuSong->SongAuthor = Content;
    }
    else if (Command == "Version")
    {
        Info->Diff->Name = Content;
    }
    else if (Command == "TitleUnicode")
    {
        if (Content.length() > 1)
            Info->OsuSong->SongName = Content;
    }
    else if (Command == "ArtistUnicode")
    {
        if (Content.length() > 1)
            Info->OsuSong->SongAuthor = Content;
    }
    else if (Command == "Creator")
    {
        Info->Diff->Author = Content;
    }
}

void ReadDifficulty(std::string line, OsuLoadInfo* Info)
{
    std::string Command = line.substr(0, line.find_first_of(":")); // Lines are Information:Content
    std::string Content = line.substr(line.find_first_of(":") + 1, line.length() - line.find_first_of(":"));
    Utility::Trim(Content);

    // We ignore everything but the key count!
    if (Command == "CircleSize")
    {
        Info->Diff->Channels = atoi(Content.c_str());
    }
    else if (Command == "SliderMultiplier")
    {
        Info->SliderVelocity = latof(Content.c_str()) * 100;
    }
    else if (Command == "HPDrainRate")
    {
        Info->TimingInfo->HP = latof(Content.c_str());
    }
    else if (Command == "OverallDifficulty")
    {
        Info->TimingInfo->OD = latof(Content.c_str());
    }
}

void ReadEvents(std::string line, OsuLoadInfo* Info)
{
    auto Spl = Utility::TokenSplit(line);

    if (Spl.size() > 1)
    {
        if (Spl[0] == "0" && Spl[1] == "0")
        {
            Utility::ReplaceAll(Spl[2], "\"", "");
            Info->OsuSong->BackgroundFilename = Spl[2];
            Info->Diff->Data->StageFile = Spl[2];
        }
        else if (Spl[0] == "5" || Spl[0] == "Sample")
        {
            Utility::ReplaceAll(Spl[3], "\"", "");

            if (Info->Sounds.find(Spl[3]) == Info->Sounds.end())
            {
                Info->Sounds[Spl[3]] = Info->last_sound_index;
                Info->last_sound_index++;
            }

            double Time = latof(Spl[1].c_str()) / 1000.0;
            int Evt = Info->Sounds[Spl[3]];
            AutoplaySound New;
            New.Time = Time;
            New.Sound = Evt;

            Info->Diff->Data->BGMEvents.push_back(New);
        }
    }
}

void ReadTiming(std::string line, OsuLoadInfo* Info)
{
    double Value;
    bool IsInherited;
    auto Spl = Utility::TokenSplit(line);

    if (Spl.size() < 2)
        return;

    if (Spl[6] == "1") // Non-inherited section
        IsInherited = false;
    else // An inherited section would be added to a velocity changes vector which would later alter speeds.
        IsInherited = true;

    int Sampleset = -1;
    int Custom = 0;
    double MeasureLen = 4;

    // We already set the value
    if (Spl.size() > 1 && !IsInherited)
        Value = 60000 / latof(Spl[1].c_str());
    else
        Value = -100 / latof(Spl[1].c_str());

    if (Spl.size() > 2)
        MeasureLen = latof(Spl[2].c_str());

    if (Spl.size() > 3)
        Sampleset = atoi(Spl[3].c_str());

    if (Spl.size() > 4)
        Custom = atoi(Spl[4].c_str());

    HitsoundSectionData SecData;
    SecData.Value = Value;
    SecData.MeasureLen = MeasureLen;
    SecData.Time = latof(Spl[0].c_str()) / 1000.0;
    SecData.Sampleset = Sampleset;
    SecData.Custom = Custom;
    SecData.IsInherited = IsInherited;
    SecData.Omit = false; // adjust if taiko bar omission is up

    Info->HitsoundSections.push_back(SecData);
}

int GetTrackFromPosition(float Position, int Channels)
{
    float Step = 512.0 / Channels;

    return static_cast<int>(Position / Step);
}

std::string SamplesetFromConstant(int Sampleset)
{
    switch (Sampleset)
    {
    case SAMPLESET_SOFT:
        return "soft";
    case SAMPLESET_DRUM:
        return "drum";
    case SAMPLESET_NORMAL:
    default:
        return "normal";
    }
}

/*
    This function is mostly correct; the main issue is that we'd have to know
    when custom = 0, that we should use 'per theme' default sounds.
    We don't have those, we don't use those, those are an osu!-ism
    so the sounds are not going to be 100% osu!-correct
    but they're going to be correct enough for virtual-mode charts to be accurate.

    SampleSetAddition is an abomination on a VSRG - so it's only left in for informative purposes.
*/
std::string GetSampleFilename(OsuLoadInfo *Info, SplitResult &Spl, int NoteType, int Hitsound, float Time)
{
    int SampleSet = 0, SampleSetAddition, CustomSample = 0;
    std::string SampleFilename;

    if (!Spl.size()) // Handle this properly, eventually.
        return "normal-hitnormal.wav";

    size_t SplSize = Spl.size();

    if (NoteType & NOTE_HOLD)
    {
        if (SplSize > 5 && Spl[5].length())
            return Spl[5];

        if (Spl.size() == 4)
        {
            SampleSet = atoi(Spl[1].c_str());
            SampleSetAddition = atoi(Spl[2].c_str());
            CustomSample = atoi(Spl[3].c_str());
        }
        else
        {
            SampleSet = atoi(Spl[0].c_str());
            SampleSetAddition = atoi(Spl[1].c_str());
            CustomSample = atoi(Spl[2].c_str());
        }

        /*
        if (SplCnt > 4)
            Volume = atoi(Spl[4].c_str()); // ignored lol
            */
    }
    else if (NoteType & NOTE_NORMAL)
    {
        if (SplSize > 4 && Spl[4].length())
            return Spl[4];

        SampleSet = atoi(Spl[0].c_str());
        if (Spl.size() > 1)
            SampleSetAddition = atoi(Spl[1].c_str());
        if (Spl.size() > 2)
            CustomSample = atoi(Spl[2].c_str());

        /*
        if (SplCnt > 3)
            Volume = atoi(Spl[3].c_str()); // ignored
            */
    }
    else if (NoteType & NOTE_SLIDER)
    {
        SampleSet = SampleSetAddition = CustomSample = 0;
    }

    std::string SampleSetGString;

    if (SampleSet)
    {
        // translate sampleset int into samplesetGString
        SampleSetGString = SamplesetFromConstant(SampleSet);
    }
    else
    {
        // get sampleset std::string from sampleset active at starttime
        int Sampleset = -1;

        for (int i = 0; i < (int)Info->HitsoundSections.size() - (int)1; i++)
        {
            if (Info->HitsoundSections[i].Time <= Time && Info->HitsoundSections[i + 1].Time < Time)
                Sampleset = Info->HitsoundSections[i].Sampleset;
            else if (Info->HitsoundSections[i + 1].Time > Time)
                Sampleset = Info->HitsoundSections[i + 1].Sampleset;
        }

        if (SampleSet == -1)
            SampleSetGString = Info->DefaultSampleset;
        else
            SampleSetGString = SamplesetFromConstant(Sampleset);
    }

    if (!CustomSample)
    {
        for (int i = 0; i < (int)Info->HitsoundSections.size() - (int)1; i++)
        {
            if (Info->HitsoundSections[i].Time <= Time && Info->HitsoundSections[i + 1].Time < Time)
                CustomSample = Info->HitsoundSections[i].Custom;
            else if (Info->HitsoundSections[i + 1].Time > Time)
                CustomSample = Info->HitsoundSections[i + 1].Custom;
        }
    }

    std::string CustomSampleGString;

    if (CustomSample)
        CustomSampleGString = Utility::IntToStr(CustomSample);

    std::string HitsoundGString;

    if (Hitsound)
    {
        switch (Hitsound)
        {
        case 1:
            HitsoundGString = "normal";
            break;
        case 2:
            HitsoundGString = "whistle";
            break;
        case 4:
            HitsoundGString = "finish";
            break;
        case 8:
            HitsoundGString = "clap";
        default:
            break;
        }
    }
    else
        HitsoundGString = "normal";

    if (CustomSample > 1)
    {
        SampleFilename = SampleSetGString + "-hit" + HitsoundGString + CustomSampleGString + ".wav";
    }
    else
        SampleFilename = SampleSetGString + "-hit" + HitsoundGString + ".wav";

    return SampleFilename;
}

void ReadObjects(std::string line, OsuLoadInfo* Info)
{
    auto Spl = Utility::TokenSplit(line);

    int Track = GetTrackFromPosition(latof(Spl[0].c_str()), Info->Diff->Channels);
    int Hitsound;
    NoteData Note;

    SplitResult Spl2;

    /*
        A few of these "ifs" are just since v11 and v12 store hold endtimes in different locations.
        Or not include some information at all...
    */
    int splitType = 5;
    if (Spl.size() == 7)
        splitType = 6;
    else if (Spl.size() == 5)
        splitType = 4;

    if (splitType != 4) // only 5 entries
        Spl2 = Utility::TokenSplit(Spl[splitType], ":");

    double startTime = latof(Spl[2].c_str()) / 1000.0;
    int NoteType = atoi(Spl[3].c_str());

    if (NoteType & NOTE_HOLD)
    {
        float endTime;
        if (splitType == 5 && Spl2.size())
            endTime = latof(Spl2[0].c_str()) / 1000.0;
        else if (splitType == 6)
            endTime = latof(Spl[5].c_str()) / 1000.0;
        else // what really? a hold that doesn't bother to tell us when it ends?
            endTime = 0;

        Note.StartTime = startTime;
        Note.EndTime = endTime;

        if (startTime > endTime)
        { // Okay then, we'll transform this into a regular note..
            Log::Printf("NoteLoaderOM: object at track %d has startTime > endTime (%f and %f)\n", Track, startTime, endTime);
            Note.EndTime = 0;

            Info->Diff->TotalScoringObjects += 1;
            Info->Diff->TotalNotes++;
        }
        else
        {
            Info->Diff->TotalScoringObjects += 2;
            Info->Diff->TotalHolds++;
        }
    }
    else if (NoteType & NOTE_NORMAL)
    {
        Note.StartTime = startTime;
        Info->Diff->TotalNotes++;
        Info->Diff->TotalScoringObjects++;
    }
    else if (NoteType & NOTE_SLIDER)
    {
        // 6=repeats 7=length
        float sliderRepeats = latof(Spl[6].c_str());
        float sliderLength = latof(Spl[7].c_str());

        float Multiplier = Info->GetSliderMultiplierAt(startTime);

        float finalSize = sliderLength * sliderRepeats * Multiplier;
        float beatDuration = (finalSize / Info->SliderVelocity);
        float bpm = (60000.0 / Info->GetBeatspaceAt(startTime));
        float finalLength = beatDuration * spb(bpm);

        if (0 > finalLength)
            Log::LogPrintf("Line %d: o!m loader warning: object at track %d has startTime > endTime (%f and %f)\n", Info->Line, Track, startTime, finalLength + startTime);

        Note.StartTime = startTime;
        Note.EndTime = finalLength + startTime;

        Info->Diff->TotalScoringObjects += 2;
        Info->Diff->TotalHolds++;
    }

    Hitsound = atoi(Spl[4].c_str());

    std::string Sample = GetSampleFilename(Info, Spl2, NoteType, Hitsound, startTime);

    if (Sample.length())
    {
        if (Info->Sounds.find(Sample) == Info->Sounds.end())
        {
            Info->Sounds[Sample] = Info->last_sound_index;
            Info->last_sound_index++;
        }

        Note.Sound = Info->Sounds[Sample];
    }

    Info->Diff->TotalObjects++;
    Info->Notes[Track].push_back(Note);

    Info->Diff->Duration = std::max(std::max(Note.StartTime, Note.EndTime) + 1, Info->Diff->Duration);
}

void MeasurizeFromTimingData(OsuLoadInfo *Info)
{
    // Keep them at the order they are declared so they don't affect the applied hitsounds.
    std::stable_sort(Info->HitsoundSections.begin(), Info->HitsoundSections.end());

    for (auto i = Info->HitsoundSections.begin(); i != Info->HitsoundSections.end(); ++i)
    {
        double TotalMeasuresThisSection;
        double SectionDurationInBeats;

        auto NextSect = i + 1;

        if (i->IsInherited || i->Omit) // Skip inherited sections.
            continue;

        while (NextSect != Info->HitsoundSections.end() && NextSect->IsInherited) // Find first non-inherited section after this one.
            ++NextSect;

        if (NextSect != Info->HitsoundSections.end()) // Okay, we've got it!
        {
            SectionDurationInBeats = bps(i->Value) * (NextSect->Time - i->Time);
        }
        else
        {
            SectionDurationInBeats = bps(i->Value) * (Info->Diff->Duration - i->Time);
        }

        TotalMeasuresThisSection = SectionDurationInBeats / i->MeasureLen;

        auto Whole = floor(TotalMeasuresThisSection);
        auto Fraction = TotalMeasuresThisSection - Whole;

        // Add the measures.
        for (auto k = 0; k < Whole; k++)
        {
            Measure Msr;
            Msr.Length = i->MeasureLen;
            Info->Diff->Data->Measures.push_back(Msr);
        }

        if (Fraction > 0)
        {
            Measure Msr;
            Msr.Length = Fraction * i->MeasureLen;
            Info->Diff->Data->Measures.push_back(Msr);
        }
    }
}

void PushNotesToMeasures(OsuLoadInfo *Info)
{
    TimingData BPS;
    Info->Diff->ProcessBPS(BPS, 0);

    for (int k = 0; k < MAX_CHANNELS; k++)
    {
        for (auto i = Info->Notes[k].begin(); i != Info->Notes[k].end(); ++i)
        {
            double Beat = QuantizeBeat(IntegrateToTime(BPS, i->StartTime));
            double CurrentBeat = 0; // Lower bound of this measure

            if (Beat < 0)
            {
                Info->Diff->Data->Measures[0].Notes[k].push_back(*i);
                continue;
            }

            for (auto m = Info->Diff->Data->Measures.begin(); m != Info->Diff->Data->Measures.end(); ++m)
            {
                double NextBeat = std::numeric_limits<double>::infinity();
                auto nextm = m + 1;

                if (nextm != Info->Diff->Data->Measures.end()) // Higher bound of this measure
                    NextBeat = CurrentBeat + m->Length;

                if (Beat >= CurrentBeat && Beat < NextBeat) // Within bounds
                {
                    m->Notes[k].push_back(*i); // Add this note to this measure.
                    break; // Stop looking for a measure.
                }

                CurrentBeat += m->Length;
            }
        }
    }
}

void Offsetize(OsuLoadInfo *Info)
{
    auto Diff = Info->Diff;
    Diff->Offset = Info->HitsoundSections[0].Time;

    for (auto i = Info->HitsoundSections.begin();
    i != Info->HitsoundSections.end();
        ++i)
    {
        i->Time -= Diff->Offset;
    }
}

enum osuReadingMode
{
    RNotKnown,
    RGeneral,
    RMetadata,
    RDifficulty,
    REvents,
    RTiming,
    RHitobjects
};

void SetReadingMode(std::string& Line, osuReadingMode& ReadingMode)
{
    if (Line == "[General]")
    {
        ReadingMode = RGeneral;
    }
    else if (Line == "[Metadata]")
    {
        ReadingMode = RMetadata;
    }
    else if (Line == "[Difficulty]")
    {
        ReadingMode = RDifficulty;
    }
    else if (Line == "[Events]")
    {
        ReadingMode = REvents;
    }
    else if (Line == "[TimingPoints]")
    {
        ReadingMode = RTiming;
    }
    else if (Line == "[HitObjects]")
    {
        ReadingMode = RHitobjects;
    }
    else if (Line[0] == '[')
        ReadingMode = RNotKnown;
}

void CopyTimingData(OsuLoadInfo* Info)
{
    for (auto S : Info->HitsoundSections)
    {
        if (S.IsInherited)
            Info->Diff->Data->Scrolls.push_back(TimingSegment(S.Time, S.Value));
        else
            Info->Diff->Timing.push_back(TimingSegment(S.Time, 60000 / S.Value));
    }
}

void NoteLoaderOM::LoadObjectsFromFile(std::string filename, std::string prefix, Song *Out)
{
#if (!defined _WIN32) || (defined STLP)
    std::ifstream filein(filename.c_str());
#else
    std::ifstream filein(Utility::Widen(filename).c_str());
#endif
    std::regex versionfmt("osu file format v(\\d+)");

    if (!filein.is_open())
        throw std::exception("Could not open file.");

    auto Diff = std::make_shared<Difficulty>();
    OsuLoadInfo Info;

    Info.TimingInfo = std::make_shared<OsuManiaTimingInfo>();
    Info.OsuSong = Out;
    Info.SliderVelocity = 1.4;
    Info.Diff = Diff;
    Info.last_sound_index = 1;

    Diff->Data = std::make_shared<DifficultyLoadInfo>();
    Diff->Data->TimingInfo = Info.TimingInfo;

    // osu! stores bpm information as the time in ms that a beat lasts.
    Diff->BPMType = Difficulty::BT_BEATSPACE;
    Out->SongDirectory = prefix;

    Diff->Filename = filename;
    Out->SongDirectory = prefix + "/";

    /*
        Just like BMS, osu!mania charts have timing data separated by files
        and a set is implied using folders.
    */

    std::string Line;

    getline(filein, Line);
    int version = -1;
    std::smatch sm;

    // "search" was picked instead of "match" since a line can have a bunch of
    // junk before the version declaration
    if (regex_search(Line.cbegin(), Line.cend(), sm, versionfmt))
        version = atoi(sm[1].str().c_str());
    else
        throw std::exception("Invalid .osu file.");

    // "osu file format v"
    if (version < 10) // why
        throw std::exception(Utility::Format("Unsupported osu! file version (%d < 10)", version).c_str());

    Info.Version = version;

    osuReadingMode ReadingMode = RNotKnown, ReadingModeOld = RNotKnown;

    try
    {
        while (filein)
        {
            Info.Line++;
            getline(filein, Line);
            Utility::ReplaceAll(Line, "\r", "");

            if (!Line.length())
                continue;

            SetReadingMode(Line, ReadingMode);

            if (ReadingMode != ReadingModeOld || ReadingMode == RNotKnown) // Skip this line since it changed modes, or it's not a valid section yet
            {
                if (ReadingModeOld == RTiming)
                    std::stable_sort(Info.HitsoundSections.begin(), Info.HitsoundSections.end());
                if (ReadingModeOld == RGeneral)
                    if (!Info.ReadAModeTag)
                        throw std::exception("Not an osu!mania chart.");
                ReadingModeOld = ReadingMode;
                continue;
            }

            switch (ReadingMode)
            {
            case RGeneral:
                if (!ReadGeneral(Line, &Info))  // don't load charts that we can't work with
                {
                    throw std::exception("osu! file unusable on raindrop.");
                }
                break;
            case RMetadata: ReadMetadata(Line, &Info); break;
            case RDifficulty: ReadDifficulty(Line, &Info); break;
            case REvents: ReadEvents(Line, &Info); break;
            case RTiming: ReadTiming(Line, &Info); break;
            case RHitobjects: ReadObjects(Line, &Info); break;
            default: break;
            }
        }

        if (Diff->TotalObjects)
        {
            // Calculate an alleged offset
            Offsetize(&Info);

            // Okay then, convert timing data into a measure-based format raindrop can use.
            MeasurizeFromTimingData(&Info);

            CopyTimingData(&Info);

            // Then copy notes into these measures.
            PushNotesToMeasures(&Info);

            // Copy all sounds we registered
            for (auto i : Info.Sounds)
                Diff->SoundList[i.second] = i.first;

            // Calculate level as NPS
            Diff->Level = Diff->TotalScoringObjects / Diff->Duration;
            Out->Difficulties.push_back(Diff);
        }
    }
    catch (std::exception &e)
    {
        // rethrow with line info
        throw std::exception(Utility::Format("Line %d: %s", Info.Line, e.what()).c_str());
    }
}