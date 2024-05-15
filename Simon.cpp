/*
* (C) 2023 badasahog. All Rights Reserved
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*/

#include <Windows.h>
#include <wrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sstream>
#include <cmath>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

#if !_HAS_CXX20
#error C++20 is required
#endif

#if !__has_include(<Windows.h>)
#error critital header Windows.h not found
#endif

HWND Window;

inline void FATAL_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			nullptr
		);

		wchar_t buffer[256];

		if (formattedErrorLength == 0)
		{
			_snwprintf_s(buffer, 256, _TRUNCATE, L"an error occured, unable to retrieve error message\nerror code: 0x%X\nlocation: line %i\n\0", hr, line);
			ShowWindow(Window, SW_HIDE);
			MessageBoxW(Window, buffer, L"Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
		}
		else
		{
			_snwprintf_s(buffer, 256, _TRUNCATE, L"an error occured: %s\nerror code: 0x%X\nlocation: line %i\n\0", messageBuffer, hr, line);
			ShowWindow(Window, SW_HIDE);
			MessageBoxW(Window, buffer, L"Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
		}
		ExitProcess(EXIT_FAILURE);
	}
}

#define FATAL_ON_FAIL(x) FATAL_ON_FAIL_IMPL(x, __LINE__)

#define FATAL_ON_FALSE(x) if((x) == FALSE) FATAL_ON_FAIL(GetLastError())

#define VALIDATE_HANDLE(x) if((x) == nullptr || (x) == INVALID_HANDLE_VALUE) FATAL_ON_FAIL(GetLastError())

using Microsoft::WRL::ComPtr;

ComPtr<ID2D1Factory> factory;
ComPtr<ID2D1HwndRenderTarget> renderTarget;

ComPtr<ID2D1SolidColorBrush> brush;
ComPtr<ID2D1SolidColorBrush> ScoreBrush;
ComPtr<ID2D1SolidColorBrush> LightGrayBrush;

ComPtr<IDWriteFactory> pDWriteFactory;

ComPtr<IDWriteTextFormat> TitleTextFormat;
ComPtr<IDWriteTextFormat> pTextFormat;
ComPtr<IDWriteTextFormat> CopyrightTextFormat;

struct Button
{
	ComPtr<ID2D1PathGeometry> Geometry;
	ComPtr<ID2D1SolidColorBrush> Brush;
	ComPtr<ID2D1SolidColorBrush> LitBrush;
};

struct Button buttons[4];

int currentLitButton = 4;
int playbackValues[256] = { 1 };
int playbackLength = 1;
int playbackLocation = 0;
int bestScore = 0;
bool mouseClicked = false;
int gameState = 0;
bool bOutstandingTimer;
bool bGeometryIsValid = false;

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept;

LARGE_INTEGER ButtonLitTicks;
LARGE_INTEGER AllButtonsOffTicks;
LARGE_INTEGER GameStateChangedTicks;
LARGE_INTEGER CurrentTimerFinished;

int windowWidth = 0;
int windowHeight = 0;

[[nodiscard]]
constexpr float frad(float degrees) noexcept{
	return degrees * 3.14159265359f / 180.0f;
}

void CreateAssets() noexcept
{
	RECT ClientRect;
	FATAL_ON_FALSE(GetClientRect(Window, &ClientRect));

	D2D1_SIZE_U size = D2D1::SizeU(ClientRect.right, ClientRect.bottom);

	FATAL_ON_FAIL(factory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(),
		D2D1::HwndRenderTargetProperties(Window, size),
		&renderTarget));

	renderTarget->SetDpi(96, 96);

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 0.0f), &brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f), &ScoreBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(.564f, .564f, .564f), &LightGrayBrush));

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.7f, 0.0f), &buttons[0].Brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.0f), &buttons[0].LitBrush));

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.0f), &buttons[1].Brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 0.0f), &buttons[1].LitBrush));

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.7f), &buttons[2].Brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f), &buttons[2].LitBrush));

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.0f, 0.0f), &buttons[3].Brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f), &buttons[3].LitBrush));


	bGeometryIsValid = false;

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.12f * windowHeight,
		L"en-us",
		&TitleTextFormat
	));

	FATAL_ON_FAIL(TitleTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.08f * windowHeight,
		L"en-us",
		&pTextFormat
	));

	FATAL_ON_FAIL(pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.05f * windowHeight,
		L"en-us",
		&CopyrightTextFormat
	));

	FATAL_ON_FAIL(CopyrightTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
}

void DrawMenu() noexcept
{
	if (renderTarget == nullptr)
	{
		CreateAssets();
	}

	renderTarget->BeginDraw();
	renderTarget->Clear();

	{
		//title
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .1f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"SIMON", 5, TitleTextFormat.Get(), textArea, ScoreBrush.Get());
	}

	POINT cursorPos;
	FATAL_ON_FALSE(GetCursorPos(&cursorPos));
	FATAL_ON_FALSE(ScreenToClient(Window, &cursorPos));

	{
		//play button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .3f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"PLAY", 4, pTextFormat.Get(), textArea, LightGrayBrush.Get());
	}

	{
		//exit button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .45f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"EXIT", 4, pTextFormat.Get(), textArea, LightGrayBrush.Get());
	}

	{
		//copyright
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .9f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * 1.f
		};
		renderTarget->DrawTextW(L"\u24B8 2023 badasahog. All Rights Reserved", 37, CopyrightTextFormat.Get(), textArea, LightGrayBrush.Get());
	}

	if (cursorPos.x > windowWidth * .4f &&
		cursorPos.x < windowWidth * .6f &&
		cursorPos.y > windowHeight * .3f &&
		cursorPos.y < windowHeight * .4f)
	{
		//play button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .3f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};

		renderTarget->DrawTextW(L"PLAY", 4, pTextFormat.Get(), textArea, brush.Get());

		if (mouseClicked)
		{
			gameState = 1;
			bOutstandingTimer = true;
			LARGE_INTEGER tickCountNow;
			FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
			CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + GameStateChangedTicks.QuadPart;
		}
	}
	else if (
		cursorPos.x > windowWidth * .4f &&
		cursorPos.x < windowWidth * .6f &&
		cursorPos.y > windowHeight * .45f &&
		cursorPos.y < windowHeight * .55f)
	{
		//exit button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .45f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"EXIT", 4, pTextFormat.Get(), textArea, brush.Get());

		if (mouseClicked)
		{
			ExitProcess(EXIT_SUCCESS);
		}
	}

	mouseClicked = false;

	FATAL_ON_FAIL(renderTarget->EndDraw());
}

void DrawGame() noexcept
{
	if (renderTarget == nullptr)
	{
		CreateAssets();
	}

	renderTarget->BeginDraw();
	renderTarget->Clear();

	FLOAT ScoreWidth = .2 * windowWidth;

	{
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = 0,
			.right = (FLOAT)windowWidth / 2 - ScoreWidth,
			.bottom = (.1f / .5f) * windowHeight
		};

		renderTarget->DrawTextW(L"score", 5, pTextFormat.Get(), textArea, ScoreBrush.Get());
	}

	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2 - ScoreWidth,
			.top = 0,
			.right = (FLOAT)windowWidth / 2,
			.bottom = (.1f / .5f) * windowHeight
		};

		std::wstring scoreText = std::to_wstring(playbackLength - 1);
		renderTarget->DrawTextW(scoreText.c_str(), scoreText.length(), pTextFormat.Get(), textArea, ScoreBrush.Get());
	}


	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2 + ScoreWidth,
			.top = 0,
			.right = (FLOAT)windowWidth,
			.bottom = (.1f / .5f) * windowHeight
		};

		std::wstring scoreText = std::to_wstring(bestScore);

		renderTarget->DrawTextW(scoreText.c_str(), scoreText.length(), pTextFormat.Get(), textArea, ScoreBrush.Get());
	}

	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2,
			.top = 0,
			.right = (FLOAT)windowWidth / 2 + ScoreWidth,
			.bottom = (.1f / .5f) * windowHeight
		};

		renderTarget->DrawTextW(L"best", 4, pTextFormat.Get(), textArea, ScoreBrush.Get());
	}


	D2D1_RECT_F boardArea =
	{
		.left = .205f / 2 * (FLOAT)windowWidth,
		.top = (.1f / .8f) * windowHeight,
		.right = (FLOAT)windowWidth - .205f / 2 * (FLOAT)windowWidth,
		.bottom = (FLOAT)windowHeight - .08f * windowHeight
	};

	float boardWidth = boardArea.right - boardArea.left;

	float fullRadius = boardWidth / 2;

	float innerCircleRadius = boardWidth * .2f;

	float outerRimRadius = fullRadius - innerCircleRadius;

	if (!bGeometryIsValid)
	{
		for (int i = 0; i < 4; i++)
		{
			const float buttonOffsetAngle = i * 90;

			FATAL_ON_FAIL(factory->CreatePathGeometry(&buttons[i].Geometry));

			ID2D1GeometrySink* pSink;

			FATAL_ON_FAIL(buttons[i].Geometry->Open(&pSink));

			pSink->SetFillMode(D2D1_FILL_MODE_WINDING);


			const float outercirclebevel = 2.5f;
			const float innercirclebevel = 4.2f;

			const float outerSliceMargin = 3.f;
			const float innerSliceMargin = 11.f;

			const float lateralMargin = outerRimRadius * .05f;

			pSink->BeginFigure(D2D1::Point2F(
				boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - outerSliceMargin)) * (fullRadius - lateralMargin),
				boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - outerSliceMargin)) * (fullRadius - lateralMargin)),
				D2D1_FIGURE_BEGIN_FILLED);



			//outer rim
			pSink->AddBezier(
				D2D1::BezierSegment(
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - outerSliceMargin)) * (fullRadius - lateralMargin),
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - outerSliceMargin)) * (fullRadius - lateralMargin)
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - outerSliceMargin)) * fullRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - outerSliceMargin)) * fullRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - outerSliceMargin - outercirclebevel)) * fullRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - outerSliceMargin - outercirclebevel)) * fullRadius
					)
				));

			pSink->AddBezier(
				D2D1::BezierSegment(
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - outerSliceMargin - outercirclebevel)) * fullRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - outerSliceMargin - outercirclebevel)) * fullRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 45)) * (fullRadius * 1.3),
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 45)) * (fullRadius * 1.3)
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + outerSliceMargin + outercirclebevel)) * fullRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + outerSliceMargin + outercirclebevel)) * fullRadius
					)
				));

			pSink->AddBezier(
				D2D1::BezierSegment(
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + outerSliceMargin + outercirclebevel)) * fullRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + outerSliceMargin + outercirclebevel)) * fullRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + outerSliceMargin)) * fullRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + outerSliceMargin)) * fullRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + outerSliceMargin)) * (fullRadius - lateralMargin),
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + outerSliceMargin)) * (fullRadius - lateralMargin)
					)
				));

			//inner rim
			pSink->AddBezier(
				D2D1::BezierSegment(
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + innerSliceMargin)) * (innerCircleRadius + lateralMargin),
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + innerSliceMargin)) * (innerCircleRadius + lateralMargin)
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + innerSliceMargin)) * innerCircleRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + innerSliceMargin)) * innerCircleRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + innerSliceMargin + innercirclebevel)) * innerCircleRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + innerSliceMargin + innercirclebevel)) * innerCircleRadius
					)
				));

			pSink->AddBezier(
				D2D1::BezierSegment(
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + innerSliceMargin + innercirclebevel)) * innerCircleRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + innerSliceMargin + innercirclebevel)) * innerCircleRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 45)) * (innerCircleRadius * 1.2),
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 45)) * (innerCircleRadius * 1.2)
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - innerSliceMargin - innercirclebevel)) * innerCircleRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - innerSliceMargin - innercirclebevel)) * innerCircleRadius
					)
				));

			pSink->AddBezier(
				D2D1::BezierSegment(
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - innerSliceMargin - innercirclebevel)) * innerCircleRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - innerSliceMargin - innercirclebevel)) * innerCircleRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - innerSliceMargin)) * innerCircleRadius,
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - innerSliceMargin)) * innerCircleRadius
					),
					D2D1::Point2F(
						boardArea.left + fullRadius + -sinf(frad(buttonOffsetAngle + 90 - innerSliceMargin)) * (innerCircleRadius + lateralMargin),
						boardArea.top + fullRadius + -cosf(frad(buttonOffsetAngle + 90 - innerSliceMargin)) * (innerCircleRadius + lateralMargin)
					)
				));


			pSink->EndFigure(D2D1_FIGURE_END_CLOSED);

			FATAL_ON_FAIL(pSink->Close());

			FATAL_ON_FAIL(pSink->Release());
		}
		

		bGeometryIsValid = true;
	}

	LARGE_INTEGER tickCountNow;
	FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));

	if (bOutstandingTimer)
	{
		if (CurrentTimerFinished.QuadPart < tickCountNow.QuadPart)
		{
			bOutstandingTimer = false;
		}

		for (int i = 0; i < 4; i++)
		{
			renderTarget->FillGeometry(buttons[i].Geometry.Get(), buttons[i].Brush.Get());
		}
	}
	else switch(gameState)
	{
	case 1:
	{
		//playback mode
		if (CurrentTimerFinished.QuadPart < tickCountNow.QuadPart)
		{
			if (currentLitButton == 4)
			{
				currentLitButton = playbackValues[playbackLocation];

				CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + ButtonLitTicks.QuadPart;

				playbackLocation++;
			}
			else
			{
				currentLitButton = 4;

				if (playbackLocation == playbackLength)
				{
					gameState = 2;
					playbackLocation = 0;
				}

				CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + AllButtonsOffTicks.QuadPart;
			}
		}

		for (int i = 0; i < 4; i++)
		{
			renderTarget->FillGeometry(buttons[i].Geometry.Get(), (i == currentLitButton) ? buttons[i].LitBrush.Get() : buttons[i].Brush.Get());
		}

		break;
	}
	case 2:
	{
		POINT cursorPos;
		FATAL_ON_FALSE(GetCursorPos(&cursorPos));
		FATAL_ON_FALSE(ScreenToClient(Window, &cursorPos));

		D2D1_POINT_2F cursor_point2f =
		{
			.x = (FLOAT)cursorPos.x,
			.y = (FLOAT)cursorPos.y
		};

		BOOL inButton;

		for (int i = 0; i < 4; i++)
		{
			buttons[i].Geometry->FillContainsPoint(cursor_point2f, nullptr, &inButton);

			renderTarget->FillGeometry(buttons[i].Geometry.Get(), inButton ? buttons[i].LitBrush.Get() : buttons[i].Brush.Get());

			if (inButton && mouseClicked)
			{
				if (i == playbackValues[playbackLocation])
				{
					playbackLocation++;
					if (playbackLocation == playbackLength)
					{
						CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + ButtonLitTicks.QuadPart;

						playbackValues[playbackLocation] = rand() % 4;
						playbackLocation = 0;
						bestScore = max(bestScore, playbackLength);
						playbackLength++;
						gameState = 1;
					}
				}
				else
				{
					playbackLength = 1;
					playbackLocation = 0;
					gameState = 1;

					bOutstandingTimer = true;
					CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + GameStateChangedTicks.QuadPart;
				}
			}
		}

		break;
	}
	}

	{
		{
			D2D1_ELLIPSE ellipse
			{
				.point =
				{
					.x = boardArea.left + boardWidth / 2,
					.y = boardArea.top + boardWidth / 2
				},
				.radiusX = boardWidth / 2,
				.radiusY = boardWidth / 2
			};

			renderTarget->DrawEllipse(&ellipse, LightGrayBrush.Get());
		}

		{
			D2D1_ELLIPSE ellipse
			{
				.point =
				{
					.x = boardArea.left + boardWidth / 2,
					.y = boardArea.top + boardWidth / 2
				},
				.radiusX = innerCircleRadius,
				.radiusY = innerCircleRadius
			};

			renderTarget->DrawEllipse(&ellipse, LightGrayBrush.Get());
		}
	}

	mouseClicked = false;

	FATAL_ON_FAIL(renderTarget->EndDraw());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	LARGE_INTEGER ProcessorFrequency;
	FATAL_ON_FALSE(QueryPerformanceFrequency(&ProcessorFrequency));

	ButtonLitTicks.QuadPart = ProcessorFrequency.QuadPart * .4;
	AllButtonsOffTicks.QuadPart = ProcessorFrequency.QuadPart * .1;
	GameStateChangedTicks.QuadPart = ProcessorFrequency.QuadPart * .5;

	{
		LARGE_INTEGER tickCountNow;
		FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
		srand(tickCountNow.LowPart);

		CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + ButtonLitTicks.QuadPart;
	}

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	UINT dpi = GetDpiForSystem();

	windowWidth = 6 * dpi;
	windowHeight = 6 * dpi;

	// Register the window class.
	constexpr wchar_t CLASS_NAME[] = L"Window CLass";

	WNDCLASS wc =
	{
		.lpfnWndProc = PreInitProc,
		.hInstance = hInstance,
		.lpszClassName = CLASS_NAME
	};
	RegisterClassW(&wc);

	// Get the required window size
	RECT windowRect =
	{
		.left = 50,
		.top = 50,
		.right = windowWidth + 50,
		.bottom = windowHeight + 50
	};

	FATAL_ON_FALSE(AdjustWindowRect(&windowRect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE));

	Window = CreateWindowExW(
		WS_EX_CLIENTEDGE,
		CLASS_NAME,
		L"Simon",
		WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		(GetSystemMetrics(SM_CXSCREEN) - (windowRect.right - windowRect.left)) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - (windowRect.bottom - windowRect.top)) / 2,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	VALIDATE_HANDLE(Window);

	FATAL_ON_FAIL(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));

	FATAL_ON_FAIL(DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		&pDWriteFactory
	));

	FATAL_ON_FALSE(ShowWindow(Window, nCmdShow));


	SetWindowLongPtrA(Window, GWLP_WNDPROC, (LONG_PTR)&WindowProc);

	SetCursor(LoadCursorW(NULL, IDC_ARROW));

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			FATAL_ON_FALSE(TranslateMessage(&Message));
			DispatchMessageW(&Message);
		}
	}

	return EXIT_SUCCESS;
}

void handleDpiChange() noexcept
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	UINT dpi = GetDpiForSystem();

	windowWidth = 6 * dpi;
	windowHeight = 6 * dpi;

	RECT windowRect =
	{
		.left = 50,
		.top = 50,
		.right = windowWidth + 50,
		.bottom = windowHeight + 50
	};

	FATAL_ON_FALSE(AdjustWindowRect(&windowRect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE));

	FATAL_ON_FALSE(SetWindowPos(
		Window,
		nullptr,
		0,
		0,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOOWNERZORDER | SWP_NOREPOSITION | SWP_NOSENDCHANGING));
}

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (message)
	{
	case WM_DPICHANGED:
		handleDpiChange();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (message)
	{

	case WM_DPICHANGED:
		handleDpiChange();
		break;
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (!IsIconic(hwnd))
			FATAL_ON_FALSE(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&WindowProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
		mouseClicked = true;
		break;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			gameState = 0;
			playbackLength = 1;
			playbackLocation = 0;
			mouseClicked = false;
		}
		break;
	case WM_DPICHANGED:
		handleDpiChange();
		[[fallthrough]];
	case WM_SIZE:
		if (IsIconic(hwnd))
		{
			FATAL_ON_FALSE(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&IdleProc) != 0);
			break;
		}
		CreateAssets();
		[[fallthrough]];
	case WM_PAINT:
		if (gameState == 0)
			DrawMenu();
		else
			DrawGame();
		break;
	default:
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}