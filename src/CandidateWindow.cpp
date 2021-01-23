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

#include "CandidateWindow.h"
#include "DrawUtils.h"
#include "TextService.h"
#include "EditSession.h"

#include <algorithm>
#include <cassert>

#include <tchar.h>
#include <windows.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>

using namespace std;
using namespace std::literals;

namespace Ime {

CandidateWindow::CandidateWindow(TextService* service, EditSession* session,
    const CandidateWindow::Theme* theme) :
    ImeWindow(service),
    shown_(false),
    candPerRow_(1),
    textWidth_(0),
    itemHeight_(0),
    currentSel_(0),
    hasResult_(false),
    useCursor_(true),
    selKeyWidth_(0),
    theme_(theme) {

    if(service->isImmersive()) { // windows 8 app mode
        margin_ = 10;
        rowSpacing_ = 8;
        colSpacing_ = 12;
    }
    else { // desktop mode
        margin_ = 5;
        rowSpacing_ = 4;
        colSpacing_ = 8;
    }

    HWND parent = service->compositionWindow(session);
    create(parent, WS_POPUP|WS_CLIPCHILDREN, WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED);
}

CandidateWindow::~CandidateWindow(void) {
}

// ITfUIElement
STDMETHODIMP CandidateWindow::GetDescription(BSTR *pbstrDescription) {
    if (!pbstrDescription)
        return E_INVALIDARG;
    *pbstrDescription = SysAllocString(L"Candidate window~");
    return S_OK;
}

// {BD7CCC94-57CD-41D3-A789-AF47890CEB29}
STDMETHODIMP CandidateWindow::GetGUID(GUID *pguid) {
    if (!pguid)
        return E_INVALIDARG;
    *pguid = { 0xbd7ccc94, 0x57cd, 0x41d3, { 0xa7, 0x89, 0xaf, 0x47, 0x89, 0xc, 0xeb, 0x29 } };
    return S_OK;
}

STDMETHODIMP CandidateWindow::Show(BOOL bShow) {
    shown_ = bShow;
    if (shown_)
        show();
    else
        hide();
    return S_OK;
}

STDMETHODIMP CandidateWindow::IsShown(BOOL *pbShow) {
    if (!pbShow)
        return E_INVALIDARG;
    *pbShow = shown_;
    return S_OK;
}

// ITfCandidateListUIElement
STDMETHODIMP CandidateWindow::GetUpdatedFlags(DWORD *pdwFlags) {
    if (!pdwFlags)
        return E_INVALIDARG;
    /// XXX update all!!!
    *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetDocumentMgr(ITfDocumentMgr **ppdim) {
    if (!textService_)
        return E_FAIL;
    return textService_->currentContext()->GetDocumentMgr(ppdim);
}

STDMETHODIMP CandidateWindow::GetCount(UINT *puCount) {
    if (!puCount)
        return E_INVALIDARG;
    *puCount = std::min<UINT>(10, items_.size());
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetSelection(UINT *puIndex) {
    assert(currentSel_ >= 0);
    if (!puIndex)
        return E_INVALIDARG;
    *puIndex = static_cast<UINT>(currentSel_);
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetString(UINT uIndex, BSTR *pbstr) {
    if (!pbstr)
        return E_INVALIDARG;
    if (uIndex >= items_.size())
        return E_INVALIDARG;
    *pbstr = SysAllocString(items_[uIndex].c_str());
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetPageIndex(UINT *puIndex, UINT uSize, UINT *puPageCnt) {
    /// XXX Always return the same single page index.
    if (!puPageCnt)
        return E_INVALIDARG;
    *puPageCnt = 1;
    if (puIndex) {
        if (uSize < *puPageCnt) {
            return E_INVALIDARG;
        }
        puIndex[0] = 0;
    }
    return S_OK;
}

STDMETHODIMP CandidateWindow::SetPageIndex(UINT *puIndex, UINT uPageCnt) {
    /// XXX Do not let app set page indices.
    if (!puIndex)
        return E_INVALIDARG;
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetCurrentPage(UINT *puPage) {
    if (!puPage)
        return E_INVALIDARG;
    *puPage = 0;
    return S_OK;
}

LRESULT CandidateWindow::wndProc(UINT msg, WPARAM wp , LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return TRUE;
            break;
        case WM_LBUTTONDOWN:
            onLButtonDown(wp, lp);
            break;
        case WM_MOUSEMOVE:
            onMouseMove(wp, lp);
            break;
        case WM_LBUTTONUP:
            onLButtonUp(wp, lp);
            break;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        default:
            return Window::wndProc(msg, wp, lp);
    }
    return 0;
}

void CandidateWindow::refresh() {
    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    SIZE size = rectSize(clientRect);

    GdiDC dcDesktop(GetDC(HWND_DESKTOP), HWND_DESKTOP);
    GdiDC dc(CreateCompatibleDC(dcDesktop));
    GdiObject bmp(CreateCompatibleBitmap(dcDesktop, size.cx, size.cy));
    GdiDCSelector bmpSelector(dc, bmp);
    paint(dc, clientRect);

    POINT ptSrc = {};
    RECT windowRect;
    GetWindowRect(hwnd_, &windowRect);
    POINT ptDst = { windowRect.left, windowRect.top };

    BLENDFUNCTION blend = bmpBlendFunction();
    ::UpdateLayeredWindow(hwnd_, dcDesktop, &ptDst, &size, dc, &ptSrc, 0, &blend, ULW_ALPHA);
}

void CandidateWindow::paint(HDC dc, const RECT& clientRect) {
    DPIScaler ds(dc);
    SIZE clientSize = rectSize(clientRect);

    theme_->background.paint(dc, clientRect);
    
    POINT pt{ theme_->contentMargin.left, theme_->contentMargin.top };
    GdiObject font(ds.createFont(theme_->font));

    if (!composition_.empty()) {
        GdiTextBlender textBlender(dc, clientSize, theme_->normalColor, 255);
        SIZE size = textBlender(composition_, { pt.x + ds.x(theme_->textMargin.left),
            pt.y + ds.y(theme_->textMargin.top) }, font);
        pt.y += size.cy + ds.y(theme_->textMargin.yspace());
    }
    
    GdiTextBlender normalTextBlender(dc, clientSize, theme_->normalColor, 255);
    for (size_t i = 0; i < items_.size(); ++i) {
        auto str = candidateString(i);
        SIZE size;
        if (useCursor_ && i == currentSel_) {
            POINT ptText{ pt.x + ds.x(theme_->textMargin.left),
                pt.y + ds.y(theme_->textMargin.top) };
            {
                GdiTextBlender highlightTextBlender(dc, clientSize,
                    theme_->highlightCandidateColor, 255);
                size = highlightTextBlender(str, ptText, font);
            }
            theme_->highlight.paint(dc, pointSizeRect(ptText, size));
        } else
            size = normalTextBlender(str, { pt.x + ds.x(theme_->textMargin.left),
                pt.y + ds.y(theme_->textMargin.top) }, font);
        pt.x += size.cx + ds.x(theme_->textMargin.xspace());
    }
}

wstring CandidateWindow::candidateString(size_t i) {
    return (selKeys_[i] ? selKeys_[i] + L"."s : L"") + items_[i];
}

void CandidateWindow::recalculateSize() {
    GdiDC dc(::GetWindowDC(hwnd_), hwnd_);
    DPIScaler ds(dc);
    SIZE totalSize{ 0, 0 };
    GdiObject font(ds.createFont(theme_->font));
    GdiDCSelector fontSelector(dc, font);
    if (!composition_.empty()) {
        SIZE size;
        ::GetTextExtentPoint32W(dc, composition_.c_str(), composition_.size(), &size);
        totalSize.cx = size.cx + ds.x(theme_->textMargin.xspace());
        totalSize.cy = size.cy + ds.y(theme_->textMargin.yspace());
    }

    SIZE candidateSize{ 0, 0 };
    for (size_t i = 0; i < items_.size(); ++i) {
        auto str = candidateString(i);
        SIZE size;
        ::GetTextExtentPoint32W(dc, str.c_str(), str.size(), &size);
        candidateSize.cx += size.cx + ds.x(theme_->textMargin.xspace());
        candidateSize.cy = (max) (candidateSize.cy, size.cy + ds.y(theme_->textMargin.yspace()));
    }
    totalSize.cx = (max) (totalSize.cx, candidateSize.cx);
    totalSize.cy = (max) (totalSize.cy, candidateSize.cy);

    totalSize.cx += ds.x(theme_->contentMargin.xspace());
    totalSize.cy += ds.y(theme_->contentMargin.yspace());
    resize(totalSize.cx, totalSize.cy);
}

void CandidateWindow::setCandPerRow(int n) {
    if(n != candPerRow_) {
        candPerRow_ = n;
        recalculateSize();
    }
}

bool CandidateWindow::filterKeyEvent(KeyEvent& keyEvent) {
    // select item with arrow keys
    int oldSel = currentSel_;
    switch(keyEvent.keyCode()) {
    case VK_UP:
        if(currentSel_ - candPerRow_ >=0)
            currentSel_ -= candPerRow_;
        break;
    case VK_DOWN:
        if(currentSel_ + candPerRow_ < items_.size())
            currentSel_ += candPerRow_;
        break;
    case VK_LEFT:
        if(currentSel_ - 1 >=0)
            --currentSel_;
        break;
    case VK_RIGHT:
        if(currentSel_ + 1 < items_.size())
            ++currentSel_;
        break;
    case VK_RETURN:
        hasResult_ = true;
        return true;
    default:
        return false;
    }
    // if currently selected item is changed, redraw
    if(currentSel_ != oldSel) {
        refresh();
        return true;
    }
    return false;
}

void CandidateWindow::setCurrentSel(int sel) {
    if(sel >= items_.size())
        sel = 0;
    if (currentSel_ != sel) {
        currentSel_ = sel;
        refresh();
    }
}

void CandidateWindow::clear() {
    items_.clear();
    selKeys_.clear();
    currentSel_ = 0;
    hasResult_ = false;
}

void CandidateWindow::setUseCursor(bool use) {
    useCursor_ = use;
    // caller will refresh
}

static wstring readIni(const filesystem::path& file, const wstring& section,
    const wstring& key, const wstring& fallback) {
    wchar_t buffer[MAX_PATH];
    ::GetPrivateProfileStringW(section.c_str(), key.c_str(), fallback.c_str(),
        &*buffer, sizeof(buffer) / sizeof(buffer[0]), file.c_str());
    return &*buffer;
}

static int readIni(const filesystem::path& file, const wstring& section,
    const wstring& key, int fallback) {
    return (int) ::GetPrivateProfileIntW(section.c_str(), key.c_str(),
        fallback, file.c_str());
}

static LOGFONT readIniFont(const filesystem::path& file, const wstring& section,
    const wstring& prefix) {
    auto defaultFont = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    LOGFONT lf;
    GetObjectW(defaultFont, sizeof(lf), &lf);
    auto name = readIni(file, section, prefix, lf.lfFaceName);
    auto i = name.find_last_of(L' ');
    if (i != wstring::npos) {
        wchar_t* p;
        auto suffix = name.substr(i + 1);
        long size = wcstol(suffix.c_str(), &p, 10);
        if (!errno && !*p && size > 0) {
            name = name.substr(0, i);
            lf.lfHeight = size;
        }
    }
    constexpr unsigned max_name_size =
        sizeof(lf.lfFaceName) / sizeof(lf.lfFaceName[0]) - 1;
    if (name.size() > max_name_size)
        name.resize(max_name_size);
    wcscpy(lf.lfFaceName, name.c_str());
    return lf;
}

static int readIniColor(const filesystem::path& file, const wstring& section,
    const wstring& key, int fallback) {
    auto str = readIni(file, section, key, wstring());
    if (!str.empty() && str[0] == L'#') str = str.substr(1);
    if (str.size() != 6) return fallback;
    for (auto c : str)
        if (!(L'0' <= c && c <= L'9' || L'a' <= c && c <= L'f' ||
            L'A' <= c && c <= L'F'))
            return fallback;
    int v = wcstol(str.c_str(), NULL, 16);
    return RGB((v & 0xFF0000) >> 16, (v & 0xFF00) >> 8, v & 0xFF);
}

void CandidateWindow::Theme::Margin::read(const std::filesystem::path& conf, const wstring& section) {
    top = readIni(conf, section, L"Top", 0);
    right = readIni(conf, section, L"Right", 0);
    bottom = readIni(conf, section, L"Bottom", 0);
    left = readIni(conf, section, L"Left", 0);
}

void CandidateWindow::Theme::StretchedImage::read(const std::filesystem::path& conf,
    const std::wstring& section, const std::filesystem::path& dir) {
    auto file = dir / readIni(conf, section, L"Image", L"image.png");
    image = make_unique<GdiWicBitmap>(file.c_str());
    margin.read(conf, section + L"/Margin");
}

void CandidateWindow::Theme::StretchedImage::paint(HDC dc, const RECT& rect) const {
    if (!image) return;

    struct Strechable {
        int value;
        bool stretched;

        int operator()(int total, DPIScaler::Value dsv) const {
            return (stretched ? total : 0) + dsv(value);
        }
    };
    struct Dim { Strechable start, size; };
    vector<Dim> xDims{
        {{ 0, false }, { margin.left, false }},
        {{ margin.left, false }, { -margin.left - margin.right, true }},
        {{ -margin.right, true }, { margin.right, false }},
    };
    vector<Dim> yDims{
        {{ 0, false }, { margin.top, false } },
        {{ margin.top, false}, { -margin.top - margin.bottom, true }},
        {{ -margin.bottom, true}, { margin.bottom, false }},
    };

    DPIScaler ds(dc);
    DPIScaler::Value nods;
    SIZE size = rectSize(rect);
    for (auto&& x : xDims) for (auto&& y : yDims) {
        image->paint(dc, pointSizeRect(
            { rect.left + x.start(size.cx, ds.x),rect.top + y.start(size.cy, ds.y) },
            { x.size(size.cx, ds.x), y.size(size.cy, ds.y) }),
            pointSizeRect(
                { x.start(image->width(), nods), y.start(image->height(), nods) },
                { x.size(image->width(), nods), y.size(image->height(), nods) }));
    }
}

CandidateWindow::Theme::Theme(const filesystem::path& dir) {
    auto conf = dir / "theme.conf";
    background.read(conf, L"InputPanel/Background", dir);
    textMargin.read(conf, L"InputPanel/TextMargin");
    contentMargin.read(conf, L"InputPanel/ContentMargin");
    font = readIniFont(conf, L"InputPanel", L"Font");
    normalColor = readIniColor(conf, L"InputPanel", L"NormalColor", 0x000000);
    highlightCandidateColor = readIniColor(conf, L"InputPanel",
        L"HighlightCandidateColor", 0x000000);
}

} // namespace Ime
