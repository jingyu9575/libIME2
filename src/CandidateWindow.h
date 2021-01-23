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

#ifndef IME_CANDIDATE_WINDOW_H
#define IME_CANDIDATE_WINDOW_H

#include "ImeWindow.h"
#include <string>
#include <vector>
#include <memory>
#include <type_traits>
#include <filesystem>
#include "ComObject.h"
#include "DrawUtils.h"
#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace Ime {

class TextService;
class EditSession;
class KeyEvent;

class CandidateWindow:
    public ImeWindow,
    public ComObject<ComInterface<ITfCandidateListUIElement>> {
public:
    struct Theme {
        struct Margin {
            int top = 0, right = 0, bottom = 0, left = 0;

            void read(const std::filesystem::path& conf, const std::wstring& section);
            auto xspace() const { return left + right; }
            auto yspace() const { return top + bottom; }
        };

        struct StretchedImage {
            std::unique_ptr<GdiWicBitmap> image;
            Margin margin;

            void read(const std::filesystem::path& conf, const std::wstring& section,
                const std::filesystem::path& dir);
            void paint(HDC dc, const RECT& rect) const;
        private:
            std::string imageData;
        };

        StretchedImage background, highlight;
        Margin textMargin, contentMargin;
        LOGFONT font;
        COLORREF normalColor, highlightCandidateColor;

        Theme(const std::filesystem::path& dir);
    };

    CandidateWindow(TextService* service, EditSession* session, const Theme* theme);

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR *pbstrDescription);
    STDMETHODIMP GetGUID(GUID *pguid);
    STDMETHODIMP Show(BOOL bShow);
    STDMETHODIMP IsShown(BOOL *pbShow);

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD *pdwFlags);
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **ppdim);
    STDMETHODIMP GetCount(UINT *puCount);
    STDMETHODIMP GetSelection(UINT *puIndex);
    STDMETHODIMP GetString(UINT uIndex, BSTR *pstr);
    STDMETHODIMP GetPageIndex(UINT *puIndex, UINT uSize, UINT *puPageCnt);
    STDMETHODIMP SetPageIndex(UINT *puIndex, UINT uPageCnt);
    STDMETHODIMP GetCurrentPage(UINT *puPage);

    void refresh() override;

    const std::vector<std::wstring>& items() const {
        return items_;
    }

    void setItems(const std::vector<std::wstring>& items, const std::vector<wchar_t>& sekKeys) {
        items_ = items;
        selKeys_ = selKeys_;
        recalculateSize();
        refresh();
    }

    void add(std::wstring item, wchar_t selKey) {
        items_.push_back(item);
        selKeys_.push_back(selKey);
    }

    void clear();

    int candPerRow() const {
        return candPerRow_;
    }
    void setCandPerRow(int n);

    virtual void recalculateSize();

    bool filterKeyEvent(KeyEvent& keyEvent);

    int currentSel() const {
        return currentSel_;
    }
    void setCurrentSel(int sel);

    wchar_t currentSelKey() const {
        return selKeys_.at(currentSel_);
    }

    bool hasResult() const {
        return hasResult_;
    }

    bool useCursor() const {
        return useCursor_;
    }

    void setUseCursor(bool use);

protected:
    LRESULT wndProc(UINT msg, WPARAM wp , LPARAM lp);
    void paint(HDC dc, const RECT& clientRect);
    std::wstring candidateString(size_t i);

protected: // COM object should not be deleted directly. calling Release() instead.
    ~CandidateWindow(void);

private:
    BOOL shown_;

    int selKeyWidth_;
    int textWidth_;
    int itemHeight_;
    int candPerRow_;
    int colSpacing_;
    int rowSpacing_;
    std::vector<wchar_t> selKeys_;
    std::vector<std::wstring> items_;
    int currentSel_;
    bool hasResult_;
    bool useCursor_;

    const Theme* theme_;
    std::wstring composition_;
};

}

#endif
