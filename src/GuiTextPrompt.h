#ifndef GUI_TEXTPROMPT_H_
#define GUI_TEXTPROMPT_H_

class BitmapFont;

namespace GUI
{

class TextPrompt
{
private:
	BitmapFont* mPromptFont;
	String		mPromptText;
	String		mBufferText;
	bool		mOpen;
public:
	TextPrompt();
	~TextPrompt();
	/* Returns 0 if closed, 1 if handling input and 2 if there's data to get from the prompt. */
	int HandleInput(int32 key, KeyEventType code, bool isMouseInput);
	void SetFont(BitmapFont* Font);
	void SetPrompt(String PromptText);
	void Render();
	void SetOpen(bool Open);
	void SwitchOpen();
	String GetContents();
};

}
#endif