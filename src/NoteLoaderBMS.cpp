#include "pch.h"

#include "GameGlobal.h"
#include "Song7K.h"

/*
	Source for implemented commands:
	http://hitkey.nekokan.dyndns.info/cmds.htm

	A huge lot of not-frequently-used commands are not going to be implemented.

	On 5K BMS, scratch is usually channel 16/26 for P1/P2
	17/27 for foot pedal- no exceptions
	keys 6 and 7 are 21/22 in bms
	keys 6 and 7 are 18/19, 28/29 in bme

	two additional extensions to BMS are planned for raindrop to introduce compatibility
	with SV changes:
	#SCROLLxx <value>
	#SPEEDxx <value> <duration>

	and are to be put under channel SV (base 36)

	Since most information is in japanese it's likely the implementation won't be perfect at the start.
*/

namespace NoteLoaderBMS{

	/* literally pasted from wikipedia */
	std::string tob36(long unsigned int value)
	{
		const char base36[37] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // off by 1 lol
		char buffer[14];
		unsigned int offset = sizeof(buffer);

		buffer[--offset] = '\0';
		do {
			buffer[--offset] = base36[value % 36];
		} while (value /= 36);

		return std::string(&buffer[offset]);
	}


	// From base 36 they're 01, 02, 03 and 09.
	const int CHANNEL_BGM = 1;
	const int CHANNEL_METER = 2;
	const int CHANNEL_BPM = 3;
	const int CHANNEL_BGABASE = 4;
	const int CHANNEL_BGAPOOR = 6;
	const int CHANNEL_BGALAYER = 7;
	const int CHANNEL_EXBPM = 8;
	const int CHANNEL_STOPS = 9;
	const int CHANNEL_BGALAYER2 = 10;
	const int CHANNEL_SCRATCH = b36toi("16");
	const int CHANNEL_SPEEDS = b36toi("SP");
	const int CHANNEL_SCROLLS = b36toi("SC");

	// Left side channels
	const int startChannelP1 = b36toi("11");
	const int startChannelLNP1 = b36toi("51");
	const int startChannelInvisibleP1 = b36toi("31");
	const int startChannelMinesP1 = b36toi("D1");

	// Right side channels
	const int startChannelP2 = b36toi("21");
	const int startChannelLNP2 = b36toi("61");
	const int startChannelInvisibleP2 = b36toi("41");
	const int startChannelMinesP2 = b36toi("E1");
	const int RELATIVE_SCRATCH_CHANNEL = b36toi("16") - b36toi("11");

	// End channels are usually xZ where X is the start (1, 2, 3 all the way to Z)
	// The first wav will always be WAV01, not WAV00 since 00 is a reserved value for "nothing"

	int TranslateTrackBME(int Channel, int relativeTo)
	{
		int relTrack = Channel - relativeTo;

		switch (relTrack)
		{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
			return relTrack + 1;
		case 5:
			return 0;
		case 6: // foot pedal is ignored.
			return VSRG::MAX_CHANNELS + 1;
		case 7:
			return 6;
		case 8:
			return 7;
		default: // Undefined
			return relTrack + 1;
		}
	}

	int TranslateTrackPMS(int Channel, int relativeTo)
	{
		int relTrack = Channel - relativeTo;

		if (relativeTo == startChannelP1 || relativeTo == startChannelLNP1)
		{
			switch (relTrack)
			{
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
				return relTrack;
			case 5:
				return 7;
			case 6:
				return 8;
			case 7:
				return 5;
			case 8:
				return 6;
			default: // Undefined
				return relTrack;
			}
		}
		else
		{
			switch (relTrack)
			{
			case 0:
			case 1:
			case 2:
			case 3:
				return relTrack;
			default: // Undefined
				return relTrack;
			}
		}
	}

	struct BMSEvent
	{
		int Event;
		float Fraction;

		bool operator<(const BMSEvent &rhs)
		{
			return Fraction < rhs.Fraction;
		}
	};

	typedef std::vector<BMSEvent> BMSEventList;

	struct BMSMeasure
	{
		// first argument is channel, second is event list
		std::map<int, BMSEventList> Events;
		float BeatDuration;

		BMSMeasure()
		{
			BeatDuration = 1; // Default duration
		}
	};

	using namespace VSRG;

	typedef std::map<int, std::string> FilenameListIndex;
	typedef std::map<int, bool> FilenameUsedIndex;
	typedef std::map<int, double> BpmListIndex;
	typedef std::vector<NoteData> NoteVector;
	typedef std::map<int, BMSMeasure> MeasureList;

	class BMSLoader
	{
		FilenameListIndex Sounds;
		FilenameListIndex Bitmaps;

		FilenameUsedIndex UsedSounds;

		BpmListIndex BPMs;
		BpmListIndex Stops;
		BpmListIndex Scrolls;

		/*
			Used channels will be bound to this list.
			The first integer is the channel.
			Second integer is the actual measure
			Syntax in other words is
			Measures[Measure].Events[Channel].Stuff
			*/
		MeasureList Measures;
		Song* Song;
		std::shared_ptr<Difficulty> Chart;
		std::vector<double> BeatAccomulation;

		int LowerBound, UpperBound;

		float startTime[MAX_CHANNELS];

		NoteData *LastNotes[MAX_CHANNELS];

		int LNObj;
		int SideBOffset;

		uint32_t RandomStack[16]; // Up to 16 nested levels.
		uint8_t CurrentNestedLevel;
		uint8_t SkipNestedLevel;
		bool Skip;

		bool IsPMS;

		bool HasBMPEvents;
		bool UsesTwoSides;
		bool IsSpecialStyle; // Uses the turntable?

		void CalculateBeatAccomulation()
		{
			int ei = (Measures.rbegin()->first) + 1;
			BeatAccomulation.clear();
			BeatAccomulation.reserve(ei);
			BeatAccomulation.push_back(0);
			for (int i = 1; i < ei; i++)
				BeatAccomulation.push_back(BeatAccomulation[i - 1] + Measures[i - 1].BeatDuration * 4);
		}

		double BeatForObj(int Measure, double Fraction)
		{
			return BeatAccomulation[Measure] + Fraction * Measures[Measure].BeatDuration * 4;
		}

		double TimeForObj(int Measure, double Fraction)
		{
			assert(Chart != nullptr);
			double Beat = BeatForObj(Measure, Fraction);
			double Time = TimeAtBeat(Chart->Timing, Chart->Offset, Beat) + StopTimeAtBeat(Chart->Data->Stops, Beat);

			return Time;
		}

		void ForAllEventsInChannel(const std::function<void(BMSEvent,int)> &fn, int Channel)
		{
			for (auto i = Measures.begin(); i != Measures.end(); ++i)
				for (auto ev = i->second.Events[Channel].begin(); ev != i->second.Events[Channel].end(); ++ev)
					fn(*ev, i->first);
		}

		void CalculateBMP(std::vector<AutoplayBMP> &BMPEvents, int Channel)
		{
			ForAllEventsInChannel([&](BMSEvent ev, int Measure) {
						AutoplayBMP New;
						New.BMP = ev.Event;
						New.Time = TimeForObj(Measure, ev.Fraction);

						BMPEvents.push_back(New);
			}, Channel);
		}

		void CalculateBPMs()
		{
			ForAllEventsInChannel([&](BMSEvent ev, int Measure) {
						double BPM = b16toi(tob36(ev.Event).c_str());
						double Beat = BeatForObj(Measure, ev.Fraction);

						Chart->Timing.push_back(TimingSegment(Beat, BPM));
			}, CHANNEL_BPM);

			ForAllEventsInChannel([&](BMSEvent ev, int Measure) {
				double BPM;
				if (BPMs.find(ev.Event) != BPMs.end())
					BPM = BPMs[ev.Event];
				else
					return;

				if (BPM == 0) return; // ignore 0 events

				double Beat = BeatForObj(Measure, ev.Fraction);
				Chart->Timing.push_back(TimingSegment(Beat, BPM));
			}, CHANNEL_EXBPM);

			// Make sure ExBPM events are in front using stable_sort.
			std::stable_sort(Chart->Timing.begin(), Chart->Timing.end());
		}

		void CalculateStops()
		{
			ForAllEventsInChannel([&](BMSEvent ev, int Measure) {
				double Beat = BeatForObj(Measure, ev.Fraction);
				double StopTimeBeats = Stops[ev.Event] / 48;
				double SectionValueStop = SectionValue(Chart->Timing, Beat);
				double SPBSection = spb(SectionValueStop);
				double StopDuration = StopTimeBeats * SPBSection; // A value of 1 is... a 48th of a beat.

				Chart->Data->Stops.push_back(TimingSegment(Beat, StopDuration));
			}, CHANNEL_STOPS);
		}

		void ForChannelRangeInMeasure(const std::function<void(BMSEvent, int)> &fn, int startChannel, MeasureList::iterator &i)
		{
			for (uint32_t channel = startChannel; channel <= (startChannel + MAX_CHANNELS); channel++)
			{
				int Track = 0;

				if (!IsPMS)
					Track = TranslateTrackBME(channel, startChannel) - LowerBound;
				else
					Track = TranslateTrackPMS(channel, startChannel);

				//if (!(Track >= 0 && Track < MAX_CHANNELS)) Utility::DebugBreak();
				if (Track >= Chart->Channels || Track < 0) continue;

				for (auto ev = i->second.Events[channel].begin(); ev != i->second.Events[channel].end(); ++ev)
					fn(*ev, Track);
			}
		}

		void CalculateMeasureSide(MeasureList::iterator &i, int TrackOffset, int startChannel, int startChannelLN,
			int startChannelMines, int startChannelInvisible, Measure &Msr)
		{
			// Standard events
			ForChannelRangeInMeasure([&](BMSEvent ev, int Track) {
				Track += TrackOffset;

				if (!ev.Event || Track >= Chart->Channels) return; // UNUSABLE event

				double Time = TimeForObj(i->first, ev.Fraction);

				Chart->Duration = std::max(double(Chart->Duration), Time);

				if (!LNObj || (LNObj != ev.Event))
				{
				degradetonote:
					NoteData Note;

					Note.StartTime = Time;
					Note.Sound = ev.Event;
					UsedSounds[ev.Event] = true;

					Chart->TotalScoringObjects++;
					Chart->TotalNotes++;
					Chart->TotalObjects++;

					Msr.Notes[Track].push_back(Note);
					if (Msr.Notes[Track].size())
						LastNotes[Track] = &Msr.Notes[Track].back();
				}
				else if (LNObj && (LNObj == ev.Event))
				{
					if (LastNotes[Track])
					{
						Chart->TotalHolds++;
						Chart->TotalScoringObjects++;
						LastNotes[Track]->EndTime = Time;
						LastNotes[Track] = nullptr;
					}
					else
						goto degradetonote;
				}
			}, startChannel, i);

			// LN events
			ForChannelRangeInMeasure([&](BMSEvent ev, int Track) {
				Track += TrackOffset;

				double Time = TimeForObj(i->first, ev.Fraction);

				if (startTime[Track] == -1)
					startTime[Track] = Time;
				else
				{
					NoteData Note;

					Chart->Duration = std::max(double(Chart->Duration), Time);

					Note.StartTime = startTime[Track];
					Note.EndTime = Time;

					Note.Sound = ev.Event;
					UsedSounds[ev.Event] = true;

					Chart->TotalScoringObjects += 2;
					Chart->TotalHolds++;
					Chart->TotalObjects++;

					Msr.Notes[Track].push_back(Note);

					startTime[Track] = -1;
				}
			}, startChannelLN, i);


			ForChannelRangeInMeasure([&](BMSEvent ev, int Track)
			{
				double Time = TimeForObj(i->first, ev.Fraction);
				NoteData Note;
				Note.StartTime = Time;
				Note.Sound = ev.Event; // Mine explosion value.
				// todo: finish

			}, startChannelMines, i);

			ForChannelRangeInMeasure([&](BMSEvent ev, int Track)
			{
				double Time = TimeForObj(i->first, ev.Fraction);
				NoteData Note;
				Note.StartTime = Time;
				Note.Sound = ev.Event; // Sound.
				Note.NoteKind = NK_INVISIBLE;
			}, startChannelInvisible, i);
		}

		void CalculateMeasure(MeasureList::iterator &i)
		{
			Measure Msr;

			Msr.Length = 4 * i->second.BeatDuration;

			// see both sides, p1 and p2
			if (!IsPMS) // or BME-type PMS
			{
				CalculateMeasureSide(i, 0, startChannelP1, startChannelLNP1, startChannelMinesP1, startChannelInvisibleP1, Msr);
				CalculateMeasureSide(i, SideBOffset, startChannelP2, startChannelLNP2, startChannelMinesP2, startChannelInvisibleP2, Msr);
			}
			else
			{
				CalculateMeasureSide(i, 0, startChannelP1, startChannelLNP1, startChannelMinesP1, startChannelInvisibleP1, Msr);
				CalculateMeasureSide(i, 5, startChannelP2 + 1, startChannelLNP2 + 1, startChannelMinesP2 + 1, startChannelInvisibleP2 + 1, Msr);
			}

			// insert it into the difficulty structure
			Chart->Data->Measures.push_back(Msr);

			for (uint8_t k = 0; k < MAX_CHANNELS; k++)
			{
				// Our old pointers are invalid by now since the Msr structures are going to go out of scope
				// Which means we must renew them, and that's better done here.

				auto q = Chart->Data->Measures.rbegin();

				while (q != Chart->Data->Measures.rend())
				{
					if ((*q).Notes[k].size()) {
						LastNotes[k] = &((*q).Notes[k].back());
						goto next_chan;
					}
					++q;
				}
			next_chan:;
			}

			if (i->second.Events[CHANNEL_BGM].size() != 0) // There are some BGM events?
			{
				for (auto ev = i->second.Events[CHANNEL_BGM].begin(); ev != i->second.Events[CHANNEL_BGM].end(); ++ev)
				{
					int Event = ev->Event;

					if (!Event) continue; // UNUSABLE event

					double Time = TimeForObj(i->first, ev->Fraction);

					AutoplaySound New;
					New.Time = Time;
					New.Sound = Event;

					UsedSounds[New.Sound] = true;

					Chart->Data->BGMEvents.push_back(New);
				}

				sort(Chart->Data->BGMEvents.begin(), Chart->Data->BGMEvents.end());
			}
		}

		void AutodetectChannelCountSide(int offset, int usedChannels[MAX_CHANNELS], 
			int startChannel, int startChannelLN, 
			int startChannelMines, int startChannelInvisible)
		{
			// Actual autodetection
			auto fn = [&](int sc, MeasureList::iterator &i)
			{
				// normal channels
				for (int curChannel = sc; curChannel <= (sc+ MAX_CHANNELS - offset); curChannel++)
				{
					if (i->second.Events.find(curChannel) == i->second.Events.end())
						continue;

					int offs;

					if (!IsPMS)
					{
						offs = TranslateTrackBME(curChannel, sc) + offset;
						if (curChannel - sc == RELATIVE_SCRATCH_CHANNEL) // Turntable is going.
							IsSpecialStyle = true;
					}
					else
					{
						offs = TranslateTrackPMS(curChannel, sc) + offset;
					}

					if (offs < MAX_CHANNELS) // A few BMSes use the foot pedal, so we need to not overflow the array.
						usedChannels[offs] = 1;
				}
			};

			for (auto i = Measures.begin(); i != Measures.end(); ++i)
			{
				fn(startChannel, i);
				fn(startChannelLN, i);
				fn(startChannelMines, i);
				fn(startChannelInvisible, i);
			}
		}

		int AutodetectChannelCount()
		{
			int usedChannels[MAX_CHANNELS] = {};
			int usedChannelsB[MAX_CHANNELS] = {};

			if (IsPMS)
				return 9;

			LowerBound = -1;
			UpperBound = 0;

			/* Autodetect channel count based off channel information */
			AutodetectChannelCountSide(0, usedChannels, startChannelP1, startChannelLNP1, startChannelMinesP1, startChannelInvisibleP1);

			/* Find the last channel we've used's index */
			int FirstIndex = -1;
			int LastIndex = 0;
			for (int i = 0; i < MAX_CHANNELS; i++)
			{
				if (usedChannels[i] != 0)
				{
					if (FirstIndex == -1) // Lowest channel being used. Used for translation back to track 0.
						FirstIndex = i;

					LastIndex = i;
				}
			}

			// Use that information to add the p2 side right next to the p1 side and have a continuous thing.
			AutodetectChannelCountSide(0, usedChannelsB, startChannelP2, startChannelLNP2, startChannelMinesP2, startChannelInvisibleP2);

			// Correct if second side starts at an offset different from zero.
			int sideBIndex = -1;

			for (int i = 0; i < MAX_CHANNELS; i++)
				if (usedChannelsB[i])
				{
					sideBIndex = i;
					break;
				}

			// We found where it starts; append that starting point to the end of the left side.

			if (sideBIndex >= 0)
			{
				for (int i = LastIndex + 1; i < MAX_CHANNELS; i++)
					usedChannels[i] |= usedChannelsB[i - LastIndex - 1];
			}

			if (FirstIndex >= 0 && sideBIndex >= 0)
				UsesTwoSides = true; // This means, when working with the second side, add the offset to the current track.

			/* Find new boundaries for used channels. This means the first channel will be the Lower Bound. */
			for (int i = 0; i < MAX_CHANNELS; i++)
			{
				if (usedChannels[i] != 0)
				{
					if (LowerBound == -1) // Lowest channel being used. Used for translation back to track 0.
						LowerBound = i;

					UpperBound = i;
				}
			}

			// We pick the range of channels we're going to use.
			int Range = UpperBound - LowerBound + 1;

			// This means, Side B offset starts from here.
			// If the last index was 7 for instance, and the first was 0, our side B offset would be 8, first channel of second side.
			// If the last index was 5 and the first was 0, side B offset would be 6.
			// While other cases would really not make much sense, they're theorically supported, anyway.
			SideBOffset = LastIndex + 1;

			// We modify it for completey unused key modes to not appear..
			if (Range < 4) // 1, 2, 3
				Range = 6;

			if (Range > 9 && Range < 12) // 10, 11
				Range = 12;

			if (Range > 12 && Range < 16) // 13, 14, 15
				Range = 16;

			return Range;
		}

		std::shared_ptr<BMSTimingInfo> TimingInfo;
	public:

		BMSLoader(VSRG::Song* song, std::shared_ptr<VSRG::Difficulty> diff, bool ispms)
		{
			for (auto k = 0; k < MAX_CHANNELS; k++)
			{
				startTime[k] = -1;
				LastNotes[k] = nullptr;
			}

			LNObj = 0;
			HasBMPEvents = false;
			Skip = false;
			CurrentNestedLevel = 0;
			UsesTwoSides = false;
			IsSpecialStyle = false;
			memset(RandomStack, 0, sizeof(RandomStack));

			IsPMS = ispms;
			Chart = diff;
			Song = song;
			// BMS uses beat-based locations for stops and BPM. (Though the beat must be calculated.)
			Chart->BPMType = Difficulty::BT_BEAT;
			Chart->IsVirtual = true;

			TimingInfo = std::make_shared<BMSTimingInfo>();
			Chart->Data->TimingInfo = TimingInfo;
		}

		void ParseEvents(const int Measure, const int BmsChannel, const std::string &Command)
		{
			auto CommandLength = Command.length() / 2;

			if (BmsChannel != CHANNEL_METER)
			{
				Measures[Measure].Events[BmsChannel].reserve(CommandLength);

				if (BmsChannel == CHANNEL_BGABASE || BmsChannel == CHANNEL_BGALAYER
					|| BmsChannel == CHANNEL_BGALAYER2 || BmsChannel == CHANNEL_BGAPOOR)
					HasBMPEvents = true;

				for (size_t i = 0; i < CommandLength; i++)
				{
					auto EventPtr = (Command.c_str() + i * 2);
					char CharEvent[3];
					int Event;
					double Fraction = double(i) / CommandLength;

					strncpy(CharEvent, EventPtr, 2); // Obtuse, but functional.
					CharEvent[2] = 0;

					Event = b36toi(CharEvent);

					if (Event == 0) // Nothing to see here?
						continue;

					BMSEvent New;

					New.Event = Event;
					New.Fraction = Fraction;

					Measures[Measure].Events[BmsChannel].push_back(New);
				}
			}
			else // Channel 2 is a measure length event.
			{
				double Event = latof(Command);
				Measures[Measure].BeatDuration = Event;
			}
		}

		bool InterpStatement(std::string Command, std::string Contents)
		{
			bool IsControlFlowCommand = false;

			// Starting off with the basics.

			do {
				if (Command == "#setrandom")
				{
					RandomStack[CurrentNestedLevel] = atoi(Contents.c_str());
				}
				else if (Command == "#random")
				{
					IsControlFlowCommand = true;

					if (Skip)
						break;

					int Limit = atoi(Contents.c_str());

					assert(CurrentNestedLevel < 16);
					assert(Limit > 1);

					RandomStack[CurrentNestedLevel] = std::randint(1, Limit);

				}
				else if (Command == "#if")
				{
					IsControlFlowCommand = true;
					CurrentNestedLevel++;

					if (Skip)
						break;

					int Var = atoi(Contents.c_str());

					assert(Var > 0);

					if (Var != RandomStack[CurrentNestedLevel - 1])
					{
						Skip = 1;
						SkipNestedLevel = CurrentNestedLevel - 1; // Once we reach this nested level, we end skipping.
					}

				}
				else if (Command == "#endif")
				{
					IsControlFlowCommand = true;
					CurrentNestedLevel--;

					if (Skip)
					{
						if (CurrentNestedLevel == SkipNestedLevel)
							Skip = 0;
					}
				}

			} while (0);

			return !IsControlFlowCommand && !Skip;
		}

		void CalculateSpeeds()
		{
			
		}

		void CalculateScrolls()
		{
			ForAllEventsInChannel([&](BMSEvent ev, int Measure)
			{
				auto Time = TimeForObj(Measure, ev.Fraction);
				if (Scrolls.find(ev.Event) != Scrolls.end())
					Chart->Data->Scrolls.push_back(TimingSegment (Time, Scrolls[ev.Event]));
				else
					Chart->Data->Scrolls.push_back(TimingSegment(Time, 1));
			}, CHANNEL_SCROLLS);
		}

		void CompileBMS()
		{
			/* To be done. */
			auto& m = Measures;
			if (m.size() == 0) return; // what
			CalculateBeatAccomulation();

			CalculateBPMs();
			CalculateStops();
			CalculateScrolls();
			CalculateSpeeds();

			if (HasBMPEvents)
			{
				auto BMP = std::make_shared < BMPEventsDetail >();
				CalculateBMP(BMP->BMPEventsLayerBase, CHANNEL_BGABASE);
				CalculateBMP(BMP->BMPEventsLayerMiss, CHANNEL_BGAPOOR);
				CalculateBMP(BMP->BMPEventsLayer, CHANNEL_BGALAYER);
				CalculateBMP(BMP->BMPEventsLayer2, CHANNEL_BGALAYER2);
				Chart->Data->BMPEvents = BMP;
			}

			Chart->Channels = AutodetectChannelCount();

			for (auto i = m.begin(); i != m.end(); ++i)
				CalculateMeasure(i);

			// Check turntable on 5/7 key singles or doubles key count
			if (Chart->Channels == 6 || Chart->Channels == 8 ||
				Chart->Channels == 12 || Chart->Channels == 16)
				Chart->Data->Turntable = IsSpecialStyle;

			/* Copy only used sounds to the sound list */
			for (auto i = Sounds.begin(); i != Sounds.end(); ++i)
				if (UsedSounds.find(i->first) != UsedSounds.end() && UsedSounds[i->first]) // This sound is used.
					Chart->SoundList[i->first] = i->second;

			if (HasBMPEvents)
				Chart->Data->BMPEvents->BMPList = Bitmaps;

		}

		void SetLNObject(int lnobj)
		{
			LNObj = lnobj;
		}

		void SetTotal(double total)
		{
			TimingInfo->GaugeTotal = total;
		}

		void SetJudgeRank(double judgerank)
		{
			TimingInfo->JudgeRank = judgerank;
		}

		void SetSound(int index, std::string command_contents)
		{
			Sounds[index] = command_contents;
		}

		void SetBMP(int index, std::string command_contents)
		{
			Bitmaps[index] = command_contents;
		}

		void SetBPM(int index, double bpm)
		{
			BPMs[index] = bpm;
		}

		void SetStop(int index, double stopval)
		{
			Stops[index] = stopval;
		}

		void SetScroll(int index, double scrollval)
		{
			Scrolls[index] = scrollval;
		}
	};

	std::string CommandSubcontents(const std::string &Command, const std::string &Line)
	{
		uint32_t len = Command.length();
		return Line.substr(len);
	}

	bool ShouldUseU8(const char* Line)
	{
		bool IsU8 = false;

		if (!utf8::starts_with_bom(Line, Line + 1024))
		{
			if (utf8::is_valid(Line, Line + 1024))
				IsU8 = true;
		}
		else
			IsU8 = true;
		return IsU8;
	}

	bool isany(char x, const char* p, ptrdiff_t len)
	{
		for (int i = 0; i < len; i++)
			if (x == p[i]) return true;

		return false;
	}

	// Returns: Out: a std::vector with all the subtitles, std::string: The title without the subtitles.
	std::string GetSubtitles(std::string SLine, std::unordered_set<std::string> &Out)
	{
		std::regex sub_reg("([~(\\[<\"].*?[\\]\\)~>\"])");
		std::smatch m;
		std::string matchL = SLine;
		while (regex_search(matchL, m, sub_reg))
		{
			Out.insert(m[1]);
			matchL = m.suffix();
		}

		std::string ret = regex_replace(SLine, sub_reg, "");
		Utility::Trim(ret);
		return ret;
	}

	std::string DifficultyNameFromSubtitles(std::unordered_set<std::string> &Subs)
	{
		std::string candidate;
		for (auto i = Subs.begin();
			i != Subs.end();
			++i)
		{
			auto Current = *i;
			Utility::ToLower(Current);
			const char* s = Current.c_str();

			if (strstr(s, "normal") || strstr(s, "5key") || strstr(s, "7key") || strstr(s, "10key")) {
				candidate = "Normal";
			}
			if (strstr(s, "another")) {
				candidate = "Another";
			}
			if (strstr(s, "ex")) {
				candidate = "EX";
			}
			if (strstr(s, "hyper") || strstr(s, "hard")) {
				candidate = "Hyper";
			}
			if (strstr(s, "light")) {
				candidate = "Light";
			}
			if (strstr(s, "beginner")) {
				candidate = "Beginner";
			}

			if (candidate.length())
			{
				Subs.erase(i);
				return candidate;
			}
		}

		// Oh, we failed then..
		return "";
	}

	void LoadObjectsFromFile(std::string filename, std::string prefix, Song *Out)
	{
#if (!defined _WIN32)
        std::ifstream filein(filename.c_str());
#else
        std::ifstream filein(Utility::Widen(filename).c_str());
#endif

        std::shared_ptr<Difficulty> Diff(new Difficulty());
        std::shared_ptr<DifficultyLoadInfo> LInfo(new DifficultyLoadInfo());
        std::regex DataDeclaration("(\\d{3})([a-zA-Z0-9]{2})");
        bool IsPMS = false;

        Diff->Filename = filename;
        Diff->Data = LInfo;
        if (Utility::Widen(filename).find(L"pms") != std::wstring::npos)
            IsPMS = true;

        std::shared_ptr<BMSLoader> Info = std::make_shared<BMSLoader>(Out, Diff, IsPMS);

        if (!filein.is_open())
            throw std::exception(("NoteLoaderBMS: Couldn't open file " + filename + "!").c_str());

        /*
            BMS files are separated always one file, one difficulty, so it'd make sense
            that every BMS 'set' might have different timing information per chart.
            While BMS specifies no 'set' support it's usually implied using folders.

            The default BME specifies is 8 channels when #PLAYER is unset, however
            the modern BMS standard specifies to ignore #PLAYER and try to figure it out
            from the amount of used channels.

            And that's what we're going to try to do.
            */

        std::unordered_set<std::string> Subs; // Subtitle list
        std::string Line;
        bool IsU8;
        char* TestU8 = new char[1025];

        int cnt = filein.readsome(TestU8, 1024);
        TestU8[cnt] = 0;

        // Sonorous UTF-8 extension
        IsU8 = ShouldUseU8(TestU8);
        delete[] TestU8;
        filein.seekg(0);

        while (filein)
        {
            getline(filein, Line);

            Utility::ReplaceAll(Line, "[\r\n]", "");

            if (Line.length() == 0 || Line[0] != '#')
                continue;

            std::string command = Line.substr(Line.find_first_of("#"), Line.find_first_of(" ") - Line.find_first_of("#"));

            Utility::ToLower(command);

#define OnCommand(x) if(command == Utility::ToLower(std::string(#x)))
#define OnCommandSub(x) if(command.substr(0, strlen(#x)) == Utility::ToLower(std::string(#x)))

            std::string CommandContents = Line.substr(Line.find_first_of(" ") + 1);
            std::string tmp;
            if (!IsU8)
                CommandContents = Utility::SJIStoU8(CommandContents);

			try {
				utf8::replace_invalid(CommandContents.begin(), CommandContents.end(), back_inserter(tmp));
			}
			catch (...) {
				if (IsU8) {
					IsU8 = false;
					CommandContents = Utility::SJIStoU8(CommandContents);
				}
			}
            CommandContents = tmp;

            if (Info->InterpStatement(command, CommandContents))
            {
				OnCommand(#EXT)
				{
					Line = CommandContents;
				}

                OnCommand(#GENRE)
                {
                    // stub
                }

                OnCommand(#SUBTITLE)
                {
                    Subs.insert(CommandContents);
                }

                OnCommand(#TITLE)
                {
                    Out->SongName = CommandContents;
                    // ltrim the std::string
                    size_t np = Out->SongName.find_first_not_of(" ");
                    if (np != std::string::npos)
                        Out->SongName = Out->SongName.substr(np);
                }

                OnCommand(#ARTIST)
                {
                    Out->SongAuthor = CommandContents;

                    size_t np = Out->SongAuthor.find_first_not_of(" ");

                    if (np != std::string::npos)
                    {
                        std::string author = Out->SongAuthor.substr(np); // I have a feeling this regex will keep growing
                        std::regex chart_author_regex("\\s*[\\/_]?\\s*(?:obj|note)\\.?\\s*[:_]?\\s*(.*)", std::regex::icase);
                        std::smatch sm;
                        if (regex_search(author, sm, chart_author_regex))
                        {
                            Diff->Author = sm[1];
                            // remove the obj. sentence
                            author = regex_replace(author, chart_author_regex, "\0");
                        }

                        Out->SongAuthor = author;
                    }
                }

                OnCommand(#BPM)
                {
                    TimingSegment Seg;
                    Seg.Time = 0;
                    Seg.Value = latof(CommandContents.c_str());
                    Diff->Timing.push_back(Seg);

                    continue;
                }

                OnCommand(#MUSIC)
                {
                    Out->SongFilename = CommandContents;
                    Diff->IsVirtual = false;
                    if (!Out->SongPreviewSource.length()) // it's unset
                        Out->SongPreviewSource = CommandContents;
                }

                OnCommand(#OFFSET)
                {
                    Diff->Offset = latof(CommandContents.c_str());
                }

                OnCommand(#PREVIEWPOINT)
                {
                    Out->PreviewTime = latof(CommandContents.c_str());
                }

                OnCommand(#PREVIEWTIME)
                {
                    Out->PreviewTime = latof(CommandContents.c_str());
                }

                OnCommand(#STAGEFILE)
                {
                    Diff->Data->StageFile = CommandContents;
                }

                OnCommand(#LNOBJ)
                {
                    Info->SetLNObject(b36toi(CommandContents.c_str()));
                }

                OnCommand(#DIFFICULTY)
                {
                    std::string dName;
                    if (Utility::IsNumeric(CommandContents.c_str()))
                    {
                        int Kind = atoi(CommandContents.c_str());

                        switch (Kind)
                        {
                        case 1:
                            dName = "Beginner";
                            break;
                        case 2:
                            dName = "Normal";
                            break;
                        case 3:
                            dName = "Hard";
                            break;
                        case 4:
                            dName = "Another";
                            break;
                        case 5:
                            dName = "Another+";
                            break;
                        default:
                            dName = "???";
                        }
                    }
                    else
                    {
                        dName = CommandContents;
                    }

                    Diff->Name = dName;
                }

                OnCommand(#BACKBMP)
                {
                    Diff->Data->StageFile = CommandContents;
                }

                OnCommand(#PREVIEW)
                {
                    Out->SongPreviewSource = CommandContents;
                }

                OnCommand(#TOTAL)
                {
                    Info->SetTotal(latof(CommandContents));
                }

                OnCommand(#PLAYLEVEL)
                {
                    Diff->Level = atoi(CommandContents.c_str());
                }

                OnCommand(#RANK)
                {
                    Info->SetJudgeRank(latof(CommandContents));
                }

                OnCommand(#MAKER)
                {
                    Diff->Author = CommandContents;
                }

                OnCommand(#SUBTITLE)
                {
                    Utility::Trim(CommandContents);
                    Subs.insert(CommandContents);
                }

                OnCommandSub(#WAV)
                {
                    std::string IndexStr = CommandSubcontents("#WAV", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetSound(Index, CommandContents);
                }

                OnCommandSub(#BMP)
                {
                    std::string IndexStr = CommandSubcontents("#BMP", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetBMP(Index, CommandContents);

                    if (Index == 1)
                    {
                        Out->BackgroundFilename = CommandContents;
                    }
                }

                OnCommandSub(#BPM)
                {
                    std::string IndexStr = CommandSubcontents("#BPM", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetBPM(Index, latof(CommandContents.c_str()));
                }

                OnCommandSub(#STOP)
                {
                    std::string IndexStr = CommandSubcontents("#STOP", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetStop(Index, latof(CommandContents.c_str()));
                }

                OnCommandSub(#EXBPM)
                {
                    std::string IndexStr = CommandSubcontents("#EXBPM", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetBPM(Index, latof(CommandContents.c_str()));
                }
				
				OnCommandSub(#SCROLL)
                {
                    std::string IndexStr = CommandSubcontents("#SCROLL", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetScroll(Index, latof(CommandContents.c_str()));
                }

                /* Else... */
                std::string MeasureCommand = Line.substr(Line.find_first_of(":") + 1);
                std::string MainCommand = Line.substr(1, 5);
                std::smatch sm;

                if (regex_match(MainCommand, sm, DataDeclaration)) // We've got work to do.
                {
                    int Measure = atoi(sm[1].str().c_str());
                    int Channel = b36toi(sm[2].str().c_str());

                    Info->ParseEvents(Measure, Channel, MeasureCommand);
                }
            }
        }

        /* When all's said and done, "compile" the bms. */
        Info->CompileBMS();

        // First try to find a suiting subtitle
        std::string NewTitle = GetSubtitles(Out->SongName, Subs);
        if (Diff->Name.length() == 0)
            Diff->Name = DifficultyNameFromSubtitles(Subs);

        // If we've got a title that's usuable then why not use it.
        if (NewTitle.length() > 0)
            Out->SongName = NewTitle.substr(0, NewTitle.find_last_not_of(" ") + 1);

        if (Subs.size() > 1)
            Out->Subtitle = Utility::Join(Subs, " ");
        else if (Subs.size() == 1)
            Out->Subtitle = *Subs.begin();

        // Okay, we didn't find a fitting subtitle, let's try something else.
        // Get actual filename instead of full path.
        filename = std::filesystem::path(filename).filename().u8string();
        if (Diff->Name.length() == 0)
        {
            size_t startBracket = filename.find_first_of("[");
            size_t endBracket = filename.find_last_of("]");

            if (startBracket != std::string::npos && endBracket != std::string::npos)
                Diff->Name = filename.substr(startBracket + 1, endBracket - startBracket - 1);

            // No brackets? Okay then, let's use the filename.
            if (Diff->Name.length() == 0)
            {
                size_t last_slash = filename.find_last_of("/");
                size_t last_dslash = filename.find_last_of("\\");
                size_t last_dir = std::max(last_slash != std::string::npos ? last_slash : 0, last_dslash != std::string::npos ? last_dslash : 0);
                Diff->Name = filename.substr(last_dir + 1, filename.length() - last_dir - 5);
            }
        }

        Out->Difficulties.push_back(Diff);
    }

    void LoadObjectsFromFile(const std::filesystem::path& filename, Song *Out)
    {
        std::shared_ptr<Difficulty> Diff(new Difficulty());
        std::shared_ptr<DifficultyLoadInfo> LInfo(new DifficultyLoadInfo());
        std::regex DataDeclaration("(\\d{3})([a-zA-Z0-9]{2})");
        bool IsPMS = false;

        Diff->Filename = filename.string().c_str();
        Diff->Data = LInfo;

        if (filename.extension() == L".pms")
        {
            IsPMS = true;
        }

        std::shared_ptr<BMSLoader> Info = std::make_shared<BMSLoader>(Out, Diff, IsPMS);

        std::ifstream filein(filename);
        if (!filein.is_open())
        {
            throw std::exception(("NoteLoaderBMS: Couldn't open file " + filename.string() + "!").c_str());
        }

        /*
        BMS files are separated always one file, one difficulty, so it'd make sense
        that every BMS 'set' might have different timing information per chart.
        While BMS specifies no 'set' support it's usually implied using folders.

        The default BME specifies is 8 channels when #PLAYER is unset, however
        the modern BMS standard specifies to ignore #PLAYER and try to figure it out
        from the amount of used channels.

        And that's what we're going to try to do.
        */

        std::unordered_set<std::string> Subs; // Subtitle list
        std::string Line;
        bool IsU8;
        char* TestU8 = new char[1025];

        int cnt = filein.readsome(TestU8, 1024);
        TestU8[cnt] = 0;

        // Sonorous UTF-8 extension
        IsU8 = ShouldUseU8(TestU8);
        delete[] TestU8;
        filein.seekg(0);

        while (filein)
        {
            getline(filein, Line);

            Utility::ReplaceAll(Line, "[\r\n]", "");

            if (Line.length() == 0 || Line[0] != '#')
                continue;

            std::string command = Line.substr(Line.find_first_of("#"), Line.find_first_of(" ") - Line.find_first_of("#"));

            Utility::ToLower(command);

#define OnCommand(x) if(command == Utility::ToLower(std::string(#x)))
#define OnCommandSub(x) if(command.substr(0, strlen(#x)) == Utility::ToLower(std::string(#x)))

            std::string CommandContents = Line.substr(Line.find_first_of(" ") + 1);
            std::string tmp;
            if (!IsU8)
                CommandContents = Utility::SJIStoU8(CommandContents);

            utf8::replace_invalid(CommandContents.begin(), CommandContents.end(), back_inserter(tmp));
            CommandContents = tmp;

            if (Info->InterpStatement(command, CommandContents))
            {
                OnCommand(#GENRE)
                {
                    // stub
                }

                OnCommand(#SUBTITLE)
                {
                    Subs.insert(CommandContents);
                }

                OnCommand(#TITLE)
                {
                    Out->SongName = CommandContents;
                    // ltrim the std::string
                    size_t np = Out->SongName.find_first_not_of(" ");
                    if (np != std::string::npos)
                        Out->SongName = Out->SongName.substr(np);
                }

                OnCommand(#ARTIST)
                {
                    Out->SongAuthor = CommandContents;

                    size_t np = Out->SongAuthor.find_first_not_of(" ");

                    if (np != std::string::npos)
                    {
                        std::string author = Out->SongAuthor.substr(np); // I have a feeling this regex will keep growing
                        std::regex chart_author_regex("\\s*[\\/_]?\\s*(?:obj|note)\\.?\\s*[:_]?\\s*(.*)", std::regex::icase);
                        std::smatch sm;
                        if (regex_search(author, sm, chart_author_regex))
                        {
                            Diff->Author = sm[1];
                            // remove the obj. sentence
                            author = regex_replace(author, chart_author_regex, "\0");
                        }

                        Out->SongAuthor = author;
                    }
                }

                OnCommand(#BPM)
                {
                    TimingSegment Seg;
                    Seg.Time = 0;
                    Seg.Value = latof(CommandContents.c_str());
                    Diff->Timing.push_back(Seg);

                    continue;
                }

                OnCommand(#MUSIC)
                {
                    Out->SongFilename = CommandContents;
                    Diff->IsVirtual = false;
                    if (!Out->SongPreviewSource.length()) // it's unset
                        Out->SongPreviewSource = CommandContents;
                }

                OnCommand(#OFFSET)
                {
                    Diff->Offset = latof(CommandContents.c_str());
                }

                OnCommand(#PREVIEWPOINT)
                {
                    Out->PreviewTime = latof(CommandContents.c_str());
                }

                OnCommand(#PREVIEWTIME)
                {
                    Out->PreviewTime = latof(CommandContents.c_str());
                }

                OnCommand(#STAGEFILE)
                {
                    Diff->Data->StageFile = CommandContents;
                }

                OnCommand(#LNOBJ)
                {
                    Info->SetLNObject(b36toi(CommandContents.c_str()));
                }

                OnCommand(#DIFFICULTY)
                {
                    std::string dName;
                    if (Utility::IsNumeric(CommandContents.c_str()))
                    {
                        int Kind = atoi(CommandContents.c_str());

                        switch (Kind)
                        {
                        case 1:
                            dName = "Beginner";
                            break;
                        case 2:
                            dName = "Normal";
                            break;
                        case 3:
                            dName = "Hard";
                            break;
                        case 4:
                            dName = "Another";
                            break;
                        case 5:
                            dName = "Another+";
                            break;
                        default:
                            dName = "???";
                        }
                    }
                    else
                    {
                        dName = CommandContents;
                    }

                    Diff->Name = dName;
                }

                OnCommand(#BACKBMP)
                {
                    Diff->Data->StageFile = CommandContents;
                }

                OnCommand(#PREVIEW)
                {
                    Out->SongPreviewSource = CommandContents;
                }

                OnCommand(#TOTAL)
                {
                    Info->SetTotal(latof(CommandContents));
                }

                OnCommand(#PLAYLEVEL)
                {
                    Diff->Level = atoi(CommandContents.c_str());
                }

                OnCommand(#RANK)
                {
                    Info->SetJudgeRank(latof(CommandContents));
                }

                OnCommand(#MAKER)
                {
                    Diff->Author = CommandContents;
                }

                OnCommand(#SUBTITLE)
                {
                    Utility::Trim(CommandContents);
                    Subs.insert(CommandContents);
                }

                OnCommandSub(#WAV)
                {
                    std::string IndexStr = CommandSubcontents("#WAV", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetSound(Index, CommandContents);
                }

                OnCommandSub(#BMP)
                {
                    std::string IndexStr = CommandSubcontents("#BMP", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetBMP(Index, CommandContents);

                    if (Index == 1)
                    {
                        Out->BackgroundFilename = CommandContents;
                    }
                }

                OnCommandSub(#BPM)
                {
                    std::string IndexStr = CommandSubcontents("#BPM", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetBPM(Index, latof(CommandContents.c_str()));
                }

                OnCommandSub(#STOP)
                {
                    std::string IndexStr = CommandSubcontents("#STOP", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetStop(Index, latof(CommandContents.c_str()));
                }

                OnCommandSub(#EXBPM)
                {
                    std::string IndexStr = CommandSubcontents("#EXBPM", command);
                    int Index = b36toi(IndexStr.c_str());
                    Info->SetBPM(Index, latof(CommandContents.c_str()));
                }

                /* Else... */
                std::string MeasureCommand = Line.substr(Line.find_first_of(":") + 1);
                std::string MainCommand = Line.substr(1, 5);
                std::smatch sm;

                if (regex_match(MainCommand, sm, DataDeclaration)) // We've got work to do.
                {
                    int Measure = atoi(sm[1].str().c_str());
                    int Channel = b36toi(sm[2].str().c_str());

                    Info->ParseEvents(Measure, Channel, MeasureCommand);
                }
            }
        }

        /* When all's said and done, "compile" the bms. */
        Info->CompileBMS();

        // First try to find a suiting subtitle
        std::string NewTitle = GetSubtitles(Out->SongName, Subs);
        if (Diff->Name.length() == 0)
            Diff->Name = DifficultyNameFromSubtitles(Subs);

        // If we've got a title that's usuable then why not use it.
        if (NewTitle.length() > 0)
            Out->SongName = NewTitle.substr(0, NewTitle.find_last_not_of(" ") + 1);

        if (Subs.size() > 1)
            Out->Subtitle = Utility::Join(Subs, " ");
        else if (Subs.size() == 1)
            Out->Subtitle = *Subs.begin();

        Diff->Name = "test";

#if 0
        // Okay, we didn't find a fitting subtitle, let's try something else.
        // Get actual filename instead of full path.
        filename = Utility::RelativeToPath(filename);
        if (Diff->Name.length() == 0)
        {
            size_t startBracket = filename.find_first_of("[");
            size_t endBracket = filename.find_last_of("]");

            if (startBracket != std::string::npos && endBracket != std::string::npos)
                Diff->Name = filename.substr(startBracket + 1, endBracket - startBracket - 1);

            // No brackets? Okay then, let's use the filename.
            if (Diff->Name.length() == 0)
            {
                size_t last_slash = filename.find_last_of("/");
                size_t last_dslash = filename.find_last_of("\\");
                size_t last_dir = max(last_slash != std::string::npos ? last_slash : 0, last_dslash != std::string::npos ? last_dslash : 0);
                Diff->Name = filename.substr(last_dir + 1, filename.length() - last_dir - 5);
            }
        }
#endif

        Out->Difficulties.push_back(Diff);
    }
}