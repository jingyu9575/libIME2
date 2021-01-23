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

#include "DrawUtils.h"

using namespace std;

void FillSolidRect(HDC dc, LPRECT rc, COLORREF color) {
    SetBkColor(dc, color);
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, rc, NULL, 0, NULL);
}

void FillSolidRect(HDC dc, int l, int t, int w, int h, COLORREF color) {
    RECT rc;
    rc.left = l;
    rc.top = t;
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    SetBkColor(dc, color);
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
}

void Draw3DBorder(HDC hdc, LPRECT rc, COLORREF light, COLORREF dark, int width) {
    MoveToEx(hdc, rc->left, rc->bottom, NULL);

    HPEN light_pen = CreatePen(PS_SOLID|PS_INSIDEFRAME, width, light);
    HGDIOBJ oldPen = SelectObject(hdc, light_pen);
    LineTo(hdc, rc->left, rc->top);
    LineTo(hdc, rc->right-width, rc->top);
    SelectObject(hdc, oldPen);
    DeleteObject(light_pen);

    HPEN dark_pen = CreatePen(PS_SOLID|PS_INSIDEFRAME, width, dark);
    oldPen = SelectObject(hdc, dark_pen);
    LineTo(hdc, rc->right-width, rc->bottom-width);
    LineTo(hdc, rc->left, rc->bottom-width);
    DeleteObject(dark_pen);
    SelectObject(hdc, oldPen);
}

void DrawBitmap(HDC dc, HBITMAP bmp, int x, int y, int w, int h, int srcx, int srcy) {
    HDC memdc = CreateCompatibleDC(dc);
    HGDIOBJ oldobj = SelectObject(memdc, bmp);
    BitBlt(dc, x, y, w, h, memdc, srcx, srcy, SRCCOPY);
    SelectObject(memdc, oldobj);
    DeleteDC(memdc);
}

HBITMAP create32bppBitmap(SIZE size, BYTE*& bits) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = size.cx;
    bmi.bmiHeader.biHeight = size.cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    return CreateDIBSection(0, &bmi, DIB_RGB_COLORS, (void**) &bits, 0, 0);
}

BLENDFUNCTION bmpBlendFunction() {
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    return bf;
}

inline BOOL alphaBlend2(HDC dcDest, POINT destPoint, SIZE destSize,
    HDC dcSource, POINT srcPoint, SIZE srcSize, BLENDFUNCTION bf) {
    return AlphaBlend(dcDest, destPoint.x, destPoint.y, destSize.cx, destSize.cy,
        dcSource, srcPoint.x, srcPoint.y, srcSize.cx, srcSize.cy, bf);
}

GdiTextBlender::GdiTextBlender(HDC dcTarget, SIZE size, COLORREF color, BYTE alpha) :
    dcTarget(dcTarget), size(size), color(color), alpha(alpha),
    bmp(create32bppBitmap(size, bits)),
    dc(CreateCompatibleDC(dcTarget)), bmpSelector(dc, bmp)
{
    RECT rect = { 0, 0, size.cx, size.cy };
    FillRect(dc, &rect, WHITE_BRUSH);
}

SIZE GdiTextBlender::operator()(const std::wstring& str, POINT point, HFONT font) {
    GdiDCSelector selector(dc, font);
    TextOutW(dc, point.x, point.y, str.c_str(), str.size());
    SIZE size;
    ::GetTextExtentPoint32W(dc, str.c_str(), str.size(), &size);
    return size;
}

GdiTextBlender::~GdiTextBlender() {
    GdiFlush();
    int count = size.cx * size.cy;
    BYTE r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    auto p = bits;
    for (int c = 0; c != count; ++c) {
        BYTE alpha = 255 - p[0];
        p[0] = b * alpha / 255;
        p[1] = g * alpha / 255;
        p[2] = r * alpha / 255;
        p[3] = alpha;
        p += 4;
    }
    alphaBlend2(dcTarget, POINT{ 0, 0 }, size,
        dc, POINT{ 0, 0 }, size, bmpBlendFunction());
}

static Ime::ComPtr<IWICImagingFactory> wicImagingFactory() {
    static auto result = [] {
        Ime::ComPtr<IWICImagingFactory> ptr;
        IWICImagingFactory** pptr = &ptr;
        CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory, (LPVOID*) pptr);
        return ptr;
    }();
    return result;
}

inline UINT dibWidthBytes(UINT bits) { return ((bits + 31) >> 5) << 2; }

GdiWicBitmap::GdiWicBitmap(const wchar_t* file) {
    Ime::ComPtr<IWICBitmapDecoder> decoder;
    Ime::ComPtr<IWICBitmapFrameDecode> frameDecode;
    Ime::ComPtr<IWICFormatConverter> convertedFrame;
    if (!wicImagingFactory() ||
        !SUCCEEDED(wicImagingFactory()->CreateDecoderFromFilename(
            file, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        !SUCCEEDED(decoder->GetFrame(0, &frameDecode)) ||
        !SUCCEEDED(frameDecode->GetSize(&width_, &height_)) ||
        !SUCCEEDED(wicImagingFactory()->CreateFormatConverter(&convertedFrame)) ||
        !SUCCEEDED(convertedFrame->Initialize(frameDecode, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
        return;
    }
    BYTE* bits;
    bmp.emplace(create32bppBitmap(SIZE{ (long) width_, (long) height_ }, bits));
    GdiDC dcDesktop(GetDC(HWND_DESKTOP), HWND_DESKTOP);
    dcBmp.emplace(CreateCompatibleDC(dcDesktop));
    bmpSelector.emplace(*dcBmp, *bmp);
    auto stride = dibWidthBytes((UINT) (width_ * 32));
    auto bufferSize = stride * height_;
    if (!SUCCEEDED(convertedFrame->CopyPixels(nullptr, stride, bufferSize, bits))) {
        dcBmp.reset();
    }
}

void GdiWicBitmap::paint(HDC dc, const RECT& destRect, const RECT& srcRect) {
    if (!*this) return;
    alphaBlend2(dc, rectPoint(destRect), rectSize(destRect), *dcBmp,
        rectPoint(srcRect), rectSize(srcRect), bmpBlendFunction());
}
