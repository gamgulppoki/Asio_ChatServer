#pragma once

#include "Types.h"

// 콘솔 ANSI 헬퍼. ChatLoop의 카톡 스타일 레이아웃을 위해
// 스크롤 영역, 커서 이동, 표시 폭 계산 등을 한 곳에 모았다.
namespace ConsoleUI
{
	struct Size { int32 Width; int32 Height; };

	// 콘솔 윈도우의 현재 폭/높이.
	Size GetSize();

	// DECSTBM: top..bottom 행만 스크롤되도록 영역 지정 (1-based).
	void SetScrollRegion(int32 iTop, int32 iBottom);
	// 스크롤 영역을 전체 화면으로 복원.
	void ResetScrollRegion();

	// 커서 이동/저장/복원.
	void MoveCursor(int32 iRow, int32 iCol);
	void SaveCursor();
	void RestoreCursor();

	// 라인/화면 클리어.
	void ClearLine();
	void ClearScreen();

	// UTF-8 문자열의 콘솔 표시 폭. ASCII=1, CJK=2, ANSI escape=0.
	int32 DisplayWidth(const String& Str);

	// 페이지 상단 헤더 박스. Title 가운데 정렬, Subtitle 있으면 회색으로 한 줄 더.
	// 현재 커서 위치부터 3~4행 출력 (cls 직후 호출 가정).
	void DrawHeaderBox(const String& Title, const String& Subtitle = "");

	// 가로 구분선 한 줄 출력 (현재 위치). Hint 가 있으면 좌측에 회색 안내 포함.
	void DrawDivider(const String& Hint = "");

	// 통일된 색상 팔레트.
	namespace Color
	{
		inline constexpr const char* Reset   = "\033[0m";
		inline constexpr const char* Success = "\033[32m";        // 초록
		inline constexpr const char* Error   = "\033[31m";        // 빨강
		inline constexpr const char* Hint    = "\033[38;5;240m";  // 회색 (안내/부가정보)
		inline constexpr const char* Accent  = "\033[38;5;208m";  // 주황 (확성기 톤)
		inline constexpr const char* Info    = "\033[36m";        // 하늘 (귓속말 톤)
	}
}