//
//    Copyright (C) 2013 - 2020 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Library General Public
//    License as published by the Free Software Foundation; either
//    version 2 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Library General Public License for more details.
//
//    You should have received a copy of the GNU Library General Public
//    License along with this library; if not, write to the
//    Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
//    Boston, MA  02110-1301, USA.
//

#ifndef IME_DRAW_UTIL_H
#define IME_DRAW_UTIL_H

#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <string>
#include "ComPtr.h"
#include <optional>

void FillSolidRect( HDC dc, LPRECT rc, COLORREF color );
void FillSolidRect( HDC dc, int l, int t, int w, int h, COLORREF color );
void Draw3DBorder(HDC hdc, LPRECT rc, COLORREF light, COLORREF dark, int width = 1);
void DrawBitmap(HDC dc, HBITMAP bmp, int x, int y, int w, int h, int srcx, int srcy );

struct NoCopy {
    NoCopy() = default;
    NoCopy(const NoCopy&) = delete;
    NoCopy& operator=(const NoCopy&) = delete;
};

template<typename T>
struct GdiObject : NoCopy {
    GdiObject(T handle) : handle(handle) {}
    ~GdiObject() { DeleteObject(handle); }
    operator T() { return handle; }
private:
    T handle;
};

struct GdiDC : NoCopy {
    GdiDC(HDC hdc) : hdc(hdc), owned(true), hwnd(NULL) {}
    GdiDC(HDC hdc, HWND hwnd) : hdc(hdc), owned(false), hwnd(hwnd) {}
    ~GdiDC() { owned ? DeleteDC(hdc) : ReleaseDC(hwnd, hdc); }
    operator HDC() { return hdc; }
private:
    HDC hdc;
    bool owned;
    HWND hwnd;
};

struct GdiDCSelector : NoCopy {
    GdiDCSelector(HDC dc, HGDIOBJ obj) : dc(dc) { old = ::SelectObject(dc, obj); }
    ~GdiDCSelector() { ::SelectObject(dc, old); }
private:
    HDC dc;
    HGDIOBJ old;
};

struct DPIScaler {
    struct Value {
        int value = 96;
        int operator()(int v) const { return MulDiv(v, value, 96); }
    };

    DPIScaler(HDC dc) : x{ GetDeviceCaps(dc, LOGPIXELSX) }, y{ GetDeviceCaps(dc, LOGPIXELSY) } {}

    HFONT createFont(LOGFONT lf) const {
        if (lf.lfHeight > 0)
            lf.lfHeight = -MulDiv(lf.lfHeight, y.value, 72);
        return CreateFontIndirectW(&lf);
    }

    Value x, y;
};

HBITMAP create32bppBitmap(SIZE size, BYTE*& bits);
BLENDFUNCTION bmpBlendFunction();

inline SIZE rectSize(const RECT& r) { return { r.right - r.left, r.bottom - r.top }; }
inline POINT rectPoint(const RECT& r) { return { r.left, r.top }; }
inline RECT pointSizeRect(const POINT& p, const SIZE& s) {
    return { p.x, p.y, p.x + s.cx, p.y + s.cy };
}

struct GdiTextBlender {
    GdiTextBlender(HDC dcTarget, SIZE size, COLORREF color, BYTE alpha);
    SIZE operator()(const std::wstring& str, POINT point, HFONT font);
    ~GdiTextBlender();

private:
    HDC dcTarget;
    SIZE size;
    COLORREF color;
    BYTE alpha;
    BYTE* bits = NULL;
    GdiObject<HBITMAP> bmp;
    GdiDC dc;
    GdiDCSelector bmpSelector;
};

struct GdiWicBitmap {
    GdiWicBitmap(const wchar_t* file);
    explicit operator bool() const { return !!dcBmp; }
    void paint(HDC dc, const RECT& destRect, const RECT& srcRect);

    auto width() const { return width_; }
    auto height() const { return height_; }
private:
    std::optional<GdiObject<HBITMAP>> bmp;
    std::optional<GdiDC> dcBmp;
    std::optional<GdiDCSelector> bmpSelector;
    UINT width_ = 0, height_ = 0;
};

#endif
