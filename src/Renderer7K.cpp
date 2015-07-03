#include "GameGlobal.h"
#include "Screen.h"

#include "GameWindow.h"
#include "Rendering.h"
#include "Sprite.h"
#include "Line.h"
#include "SceneEnvironment.h"

#include "ScreenGameplay7K.h"
#include "Noteskin.h"

using namespace VSRG;

void ScreenGameplay7K::Render()
{
	BGA->Render();

	Animations->DrawUntilLayer(13);

	DrawMeasures();

	Animations->DrawFromLayer(14);
}

void ScreenGameplay7K::DrawBarlines(float rPos)
{
	for (auto i = MeasureBarlines.begin();
		i != MeasureBarlines.end();
		++i)
	{
		float realV = rPos - (*i) * SpeedMultiplier + BarlineOffset;
		if (realV > 0 && realV < ScreenWidth)
		{
			Barline->SetLocation (Vec2(BarlineX, realV), Vec2(BarlineX + BarlineWidth, realV));
			Barline->Render();
		}
	}
}

bool ShouldDrawNoteInScreen(TrackNote& T, float SpeedMultiplier, float FieldDisplacement, float &Vertical, float &VerticalHoldEnd, bool Upscroll)
{
	if (!T.IsVisible())
		return false;

	Vertical = (T.GetVertical() * SpeedMultiplier + FieldDisplacement);
	if (T.IsHold())
		VerticalHoldEnd = (T.GetHoldEndVertical() * SpeedMultiplier + FieldDisplacement);

	return true;
}

Mat4 id;

void ScreenGameplay7K::DrawMeasures()
{
	float FieldDisplacement;

	FieldDisplacement = CurrentVertical * SpeedMultiplier + JudgmentLinePos;

	if (BarlineEnabled)
		DrawBarlines(FieldDisplacement);

	// Set some parameters...
	SetShaderParameters(false, false, true, true, false, false, RealHiddenMode);

	// Sudden = 1, Hidden = 2, flashlight = 3 (Defined in the shader)
	if (RealHiddenMode)
	{
		WindowFrame.SetUniform(U_HIDLOW, HideClampLow);
		WindowFrame.SetUniform(U_HIDHIGH, HideClampHigh);
		WindowFrame.SetUniform(U_HIDFAC, HideClampFactor);
		WindowFrame.SetUniform(U_HIDSUM, HideClampSum);
	}

	WindowFrame.SetUniform(U_SIM, &id[0][0]);
	WindowFrame.SetUniform(U_TRANM, &id[0][0]);
	WindowFrame.SetUniform(U_SMULT, SpeedMultiplier);

	SetPrimitiveQuadVBO();

	for (uint32 k = 0; k < CurrentDiff->Channels; k++)
	{
		/* Find the location of the first/next visible regular note */
		auto StartPred = [&](const TrackNote &A, double _) -> bool {
			auto Vert = A.GetVertical() * SpeedMultiplier + FieldDisplacement;
			return Vert > _;
		};

		auto Start = std::lower_bound(NotesByChannel[k].begin(), NotesByChannel[k].end(), ScreenHeight, StartPred);

		// Locate the first hold that we can draw in this range
		auto rStart = std::reverse_iterator<vector<TrackNote>::iterator>(Start);
		for (auto i = rStart; i != NotesByChannel[k].rend(); ++i)
		{
			TrackNote &T = *i;
			if (i->IsHold() && i->IsVisible()) {
				auto Vert = i->GetVertical() * SpeedMultiplier + FieldDisplacement;
				auto VertEnd = i->GetHoldEndVertical() * SpeedMultiplier + FieldDisplacement;
				if (IntervalsIntersect(0, ScreenHeight, min(Vert, VertEnd), max(Vert, VertEnd)))
				{
					Start = i.base() - 1;
					break;
				}
			}
		}

		// Find the note that is out of the drawing range
		auto EndPred = [&](double _, const TrackNote &A)->bool {
			auto Vert = A.GetVertical() * SpeedMultiplier + FieldDisplacement;
			return _ > Vert;
		};

		auto End = std::upper_bound(NotesByChannel[k].begin(), NotesByChannel[k].end(), 0, EndPred);
		
		// Now, draw it.
		for (auto m = Start; m != End; ++m)
		{
			// Is there a better way to do this that doesn't involve recalculating this every note?
			float Vertical = 0;
			float VerticalHoldEnd;

			if (!ShouldDrawNoteInScreen(*m, SpeedMultiplier, FieldDisplacement, Vertical, VerticalHoldEnd, Upscroll))
				continue; // If this is not visible, we move on to the next note. 

			// Assign our matrix.
			WindowFrame.SetUniform(U_MVP, &id[0][0]);

			// We draw the body first, so that way the heads get drawn on top
			if (m->IsHold())
			{
				float Pos = (VerticalHoldEnd + Vertical) / 2;
				float Size = m->GetHoldSize() * SpeedMultiplier;
				int Level = 1;

				if (!m->IsEnabled() && !m->FailedHit())
					Level = 2;
				if (!m->IsEnabled() && m->FailedHit())
					Level = 0;

				Noteskin::DrawHoldBody(k, Pos, Size, Level);
				Noteskin::DrawHoldTail(*m, k, VerticalHoldEnd);
				
				// LR2 style keep-on-the-judgment-line
				if ( (Vertical > VerticalHoldEnd && Upscroll || Vertical < VerticalHoldEnd && !Upscroll) && m->IsJudgable() )
					Noteskin::DrawHoldHead(*m, k, JudgmentLinePos);
				else
					Noteskin::DrawHoldHead(*m, k, Vertical);
			} else
			{
				if ((Vertical < JudgmentLinePos && Upscroll || Vertical >= JudgmentLinePos && !Upscroll) && m->IsJudgable())
					Noteskin::DrawNote(*m, k, JudgmentLinePos);
				else
					Noteskin::DrawNote(*m, k, Vertical);
			}
		}
	}

	/* Clean up */
	MultiplierChanged = false;
	FinalizeDraw();
}
