#include "ConsoleUI.h"
#include <Windows.h>
#include <iostream>

namespace ConsoleUI
{
	// 현재 콘솔 윈도우 크기. 실패 시 80x25 기본값 반환.
	Size GetSize()
	{
		CONSOLE_SCREEN_BUFFER_INFO Csbi;
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &Csbi))
		{
			return {
				Csbi.srWindow.Right - Csbi.srWindow.Left + 1,
				Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1
			};
		}
		return { 80, 25 };
	}

	// DECSTBM: top..bottom 행만 스크롤. 그 바깥은 고정.
	void SetScrollRegion(int32 iTop, int32 iBottom)
	{
		std::cout << "\033[" << iTop << ";" << iBottom << "r" << std::flush;
	}

	// 스크롤 영역을 전체 화면으로 복원.
	void ResetScrollRegion()
	{
		std::cout << "\033[r" << std::flush;
	}

	// 1-based 행/열로 커서 이동.
	void MoveCursor(int32 iRow, int32 iCol)
	{
		std::cout << "\033[" << iRow << ";" << iCol << "H" << std::flush;
	}

	// 커서 위치 저장 (DECSC).
	void SaveCursor()
	{
		std::cout << "\033[s" << std::flush;
	}

	// 커서 위치 복원 (DECRC).
	void RestoreCursor()
	{
		std::cout << "\033[u" << std::flush;
	}

	// 현재 줄 전체 클리어.
	void ClearLine()
	{
		std::cout << "\033[2K" << std::flush;
	}

	// 화면 전체 클리어 + 커서 (1,1) 이동.
	void ClearScreen()
	{
		std::cout << "\033[2J\033[H" << std::flush;
	}

	// 헤더 박스: ┌─┐ / 가운데 정렬 타이틀 / (회색 서브타이틀) / └─┘.
	void DrawHeaderBox(const String& Title, const String& Subtitle)
	{
		const auto Sz = GetSize();
		const int32 InnerW = Sz.Width - 2;

		std::cout << "┌";
		for (int32 i = 0; i < InnerW; ++i) std::cout << "─";
		std::cout << "┐\n";

		auto DrawCenteredLine = [&](const String& Text, const char* ColorEsc)
		{
			const int32 Tw = DisplayWidth(Text);
			const int32 LeftPad = (InnerW - Tw) / 2;
			const int32 RightPad = InnerW - Tw - LeftPad;
			std::cout << "│";
			for (int32 i = 0; i < LeftPad; ++i) std::cout << " ";
			if (ColorEsc) std::cout << ColorEsc;
			std::cout << Text;
			if (ColorEsc) std::cout << Color::Reset;
			for (int32 i = 0; i < RightPad; ++i) std::cout << " ";
			std::cout << "│\n";
		};

		DrawCenteredLine(Title, nullptr);
		if (!Subtitle.empty())
			DrawCenteredLine(Subtitle, Color::Hint);

		std::cout << "└";
		for (int32 i = 0; i < InnerW; ++i) std::cout << "─";
		std::cout << "┘\n" << std::flush;
	}

	// 회색 가로 구분선. Hint 있으면 "─ Hint ─────..." 형태로.
	void DrawDivider(const String& Hint)
	{
		const auto Sz = GetSize();
		std::cout << Color::Hint;
		if (Hint.empty())
		{
			for (int32 i = 0; i < Sz.Width; ++i) std::cout << "─";
		}
		else
		{
			std::cout << "─ " << Hint << " ";
			const int32 Used = 3 + DisplayWidth(Hint);
			for (int32 i = 0; i < Sz.Width - Used; ++i) std::cout << "─";
		}
		std::cout << Color::Reset << "\n" << std::flush;
	}

	// UTF-8 표시 폭. ANSI escape 시퀀스는 0칸,
	// ASCII = 1, 2바이트 시퀀스 = 1, CJK/이모지 = 2 로 계산.
	int32 DisplayWidth(const String& Str)
	{
		int32 iWidth = 0;
		size_t i = 0;
		while (i < Str.size())
		{
			unsigned char c = static_cast<unsigned char>(Str[i]);

			// ANSI escape: ESC '[' ... 알파벳('@'~'~') 으로 종료
			if (c == 0x1B && i + 1 < Str.size() && Str[i + 1] == '[')
			{
				i += 2;
				while (i < Str.size() && !(Str[i] >= '@' && Str[i] <= '~'))
					++i;
				if (i < Str.size())
					++i;
				continue;
			}

			if (c < 0x80)
			{
				iWidth += 1;
				i += 1;
			}
			else if ((c & 0xE0) == 0xC0)
			{
				iWidth += 1;
				i += 2;
			}
			else if ((c & 0xF0) == 0xE0)
			{
				iWidth += 2;
				i += 3;
			}
			else if ((c & 0xF8) == 0xF0)
			{
				iWidth += 2;
				i += 4;
			}
			else
			{
				i += 1;
			}
		}
		return iWidth;
	}
}