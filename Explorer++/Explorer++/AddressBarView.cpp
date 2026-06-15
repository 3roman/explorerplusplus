// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "AddressBarView.h"
#include "AddressBarViewDelegate.h"
#include "TestHelper.h"
#include "../Helper/DpiCompatibility.h"
#include "../Helper/WindowHelper.h"
#include "../Helper/WindowSubclass.h"
#include <Shlwapi.h>

AddressBarView *AddressBarView::Create(HWND parent, const Config *config)
{
	return new AddressBarView(parent, config);
}

AddressBarView::AddressBarView(HWND parent, const Config *config) :
	m_hwnd(CreateAddressBar(parent)),
	m_breadcrumbWindow(CreateBreadcrumbWindow(m_hwnd)),
	m_fontSetter(m_hwnd, config)
{
	HIMAGELIST smallIcons;
	BOOL res = Shell_GetImageLists(nullptr, &smallIcons);
	CHECK(res);
	SendMessage(m_hwnd, CBEM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(smallIcons));

	m_windowSubclasses.push_back(std::make_unique<WindowSubclass>(m_hwnd,
		std::bind_front(&AddressBarView::ComboBoxExSubclass, this)));
	m_windowSubclasses.push_back(std::make_unique<WindowSubclass>(m_breadcrumbWindow,
		std::bind_front(&AddressBarView::BreadcrumbSubclass, this)));

	auto edit = GetEditControl();
	m_windowSubclasses.push_back(std::make_unique<WindowSubclass>(edit,
		std::bind_front(&AddressBarView::EditSubclass, this)));

	HRESULT hr = SHAutoComplete(edit, SHACF_FILESYSTEM | SHACF_AUTOSUGGEST_FORCE_ON);
	DCHECK(SUCCEEDED(hr));

	m_windowSubclasses.push_back(std::make_unique<WindowSubclass>(parent,
		std::bind_front(&AddressBarView::ParentSubclass, this)));

	m_fontSetter.fontUpdatedSignal.AddObserver(
		std::bind(&AddressBarView::OnFontOrDpiUpdated, this));

	LayoutBreadcrumbWindow();
}

HWND AddressBarView::CreateAddressBar(HWND parent)
{
	// Note that a non 0 height needs to be passed in here. That's because the control will
	// interpret the height as the combined height of the edit control plus dropdown (see
	// https://devblogs.microsoft.com/oldnewthing/20060310-17/?p=31973).
	//
	// If the height is 0, the edit control will still display normally, but the dropdown will
	// seemingly never appear, since its height will be 0.
	return CreateWindowEx(WS_EX_TOOLWINDOW, WC_COMBOBOXEX, L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_CLIPSIBLINGS
			| WS_CLIPCHILDREN,
		0, 0, 0, 200, parent, nullptr, GetModuleHandle(nullptr), nullptr);
}

HWND AddressBarView::CreateBreadcrumbWindow(HWND parent)
{
	return CreateWindow(WC_STATIC, L"",
		WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | SS_NOTIFY, 0, 0, 0, 0, parent, nullptr,
		GetModuleHandle(nullptr), nullptr);
}

LRESULT AddressBarView::ComboBoxExSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_SIZE:
		LayoutBreadcrumbWindow();
		break;

	case WM_DPICHANGED_AFTERPARENT:
		OnFontOrDpiUpdated();
		break;

	case WM_SETFOCUS:
		SetEditMode(true, false);

		if (m_delegate)
		{
			m_delegate->OnFocused();
		}
		break;

	case WM_NCDESTROY:
		OnNcDestroy();
		return 0;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarView::BreadcrumbSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		OnBreadcrumbPaint();
		return 0;

	case WM_LBUTTONDOWN:
		OnBreadcrumbLeftButtonDown({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		return 0;

	case WM_SETCURSOR:
	{
		POINT pt;
		GetCursorPos(&pt);
		MapWindowPoints(nullptr, hwnd, &pt, 1);

		if (HitTestBreadcrumbItem(pt) != -1)
		{
			SetCursor(LoadCursor(nullptr, IDC_HAND));
			return TRUE;
		}
	}
	break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarView::EditSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (m_delegate && m_delegate->OnKeyPressed(static_cast<UINT>(wParam)))
		{
			return 0;
		}
		break;

	case WM_SETFOCUS:
		SetEditMode(true, false);

		if (m_delegate)
		{
			m_delegate->OnFocused();
		}
		break;

	case WM_KILLFOCUS:
		if (reinterpret_cast<HWND>(wParam) != m_hwnd
			&& reinterpret_cast<HWND>(wParam) != m_breadcrumbWindow)
		{
			SetEditMode(false, false);
		}
		break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarView::ParentSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NOTIFY:
		if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == m_hwnd)
		{
			switch (reinterpret_cast<LPNMHDR>(lParam)->code)
			{
			case CBEN_DRAGBEGIN:
				if (m_delegate)
				{
					m_delegate->OnBeginDrag();
				}
				break;
			}
		}
		break;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void AddressBarView::SetDelegate(AddressBarViewDelegate *delegate)
{
	m_delegate = delegate;
}

HWND AddressBarView::GetHWND() const
{
	return m_hwnd;
}

std::wstring AddressBarView::GetText() const
{
	return GetWindowString(m_hwnd);
}

bool AddressBarView::IsTextModified() const
{
	return SendMessage(GetEditControl(), EM_GETMODIFY, 0, 0);
}

void AddressBarView::SelectAllText()
{
	SendMessage(GetEditControl(), EM_SETSEL, 0, -1);
}

void AddressBarView::UpdateTextAndIcon(const std::optional<std::wstring> &optionalText,
	int iconIndex)
{
	UpdateTextAndIcon(optionalText, iconIndex, std::nullopt);
}

void AddressBarView::UpdateTextAndIcon(const std::optional<std::wstring> &optionalText,
	int iconIndex, std::optional<std::vector<BreadcrumbSegment>> breadcrumbSegments)
{
	COMBOBOXEXITEM cbItem = {};
	cbItem.mask = CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT;
	cbItem.iItem = -1;
	cbItem.iImage = iconIndex;
	cbItem.iSelectedImage = iconIndex;
	cbItem.iIndent = 1;

	if (optionalText)
	{
		WI_SetFlag(cbItem.mask, CBEIF_TEXT);
		cbItem.pszText = const_cast<LPWSTR>(optionalText->c_str());

		m_currentText = *optionalText;
	}

	if (breadcrumbSegments)
	{
		SetBreadcrumbSegments(*breadcrumbSegments);
		SetEditMode(false, false);
	}

	auto res = SendMessage(m_hwnd, CBEM_SETITEM, 0, reinterpret_cast<LPARAM>(&cbItem));
	DCHECK(res);
}

void AddressBarView::RevertText()
{
	SendMessage(m_hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(m_currentText.c_str()));
}

void AddressBarView::ShowBreadcrumb()
{
	SetEditMode(false, false);
}

void AddressBarView::FocusTextEntry(bool selectText)
{
	SetEditMode(true, selectText);
}

HWND AddressBarView::GetEditControl() const
{
	return reinterpret_cast<HWND>(SendMessage(m_hwnd, CBEM_GETEDITCONTROL, 0, 0));
}

void AddressBarView::SetBreadcrumbSegments(const std::vector<BreadcrumbSegment> &segments)
{
	m_breadcrumbItems.clear();
	m_breadcrumbItems.reserve(segments.size());

	for (const auto &segment : segments)
	{
		m_breadcrumbItems.push_back({ segment.text, segment.pidl });
	}

	InvalidateRect(m_breadcrumbWindow, nullptr, TRUE);
}

void AddressBarView::LayoutBreadcrumbWindow()
{
	RECT clientRect;
	GetClientRect(m_hwnd, &clientRect);

	auto dropdownWidth = GetSystemMetrics(SM_CXVSCROLL);
	auto margin = DpiCompatibility::GetInstance().ScaleValue(m_hwnd, 2);

	SetWindowPos(m_breadcrumbWindow, HWND_TOP, margin, margin,
		std::max(0, GetRectWidth(&clientRect) - dropdownWidth - (2 * margin)),
		std::max(0, GetRectHeight(&clientRect) - (2 * margin)), SWP_NOACTIVATE);
	UpdateBreadcrumbItemBounds();
}

void AddressBarView::SetEditMode(bool editMode, bool selectText)
{
	if (!editMode && m_breadcrumbItems.empty())
	{
		ShowWindow(m_breadcrumbWindow, SW_HIDE);
		return;
	}

	ShowWindow(m_breadcrumbWindow, editMode ? SW_HIDE : SW_SHOWNA);

	if (editMode)
	{
		auto editControl = GetEditControl();

		if (GetFocus() != editControl)
		{
			SetFocus(editControl);
		}

		if (selectText)
		{
			SelectAllText();
		}
	}
	else
	{
		SetWindowPos(m_breadcrumbWindow, HWND_TOP, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
}

void AddressBarView::UpdateBreadcrumbItemBounds()
{
	HDC hdc = GetDC(m_breadcrumbWindow);

	if (!hdc)
	{
		return;
	}

	HGDIOBJ oldFont = nullptr;
	HFONT font = reinterpret_cast<HFONT>(SendMessage(m_hwnd, WM_GETFONT, 0, 0));

	if (font)
	{
		oldFont = SelectObject(hdc, font);
	}

	RECT clientRect;
	GetClientRect(m_breadcrumbWindow, &clientRect);
	UpdateBreadcrumbItemBounds(hdc, clientRect);

	if (oldFont)
	{
		SelectObject(hdc, oldFont);
	}

	ReleaseDC(m_breadcrumbWindow, hdc);
}

void AddressBarView::UpdateBreadcrumbItemBounds(HDC hdc, const RECT &clientRect)
{
	auto padding = DpiCompatibility::GetInstance().ScaleValue(m_breadcrumbWindow, 6);
	auto separatorWidth = DpiCompatibility::GetInstance().ScaleValue(m_breadcrumbWindow, 14);

	RECT bounds = clientRect;
	bounds.left += padding;

	for (size_t i = 0; i < m_breadcrumbItems.size(); i++)
	{
		auto &item = m_breadcrumbItems[i];
		item.bounds = {};

		if (bounds.left >= clientRect.right)
		{
			continue;
		}

		auto isLastItem = (i == m_breadcrumbItems.size() - 1);

		SIZE textSize;
		GetTextExtentPoint32(hdc, item.text.c_str(), static_cast<int>(item.text.size()),
			&textSize);

		bounds.right =
			std::min(clientRect.right, bounds.left + textSize.cx + (2 * padding));
		item.bounds = bounds;

		bounds.left = bounds.right;

		if (!isLastItem && bounds.left < clientRect.right)
		{
			bounds.left += separatorWidth;
		}
	}
}

void AddressBarView::OnBreadcrumbPaint()
{
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(m_breadcrumbWindow, &ps);

	RECT clientRect;
	GetClientRect(m_breadcrumbWindow, &clientRect);

	FillRect(hdc, &clientRect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

	HFONT font = reinterpret_cast<HFONT>(SendMessage(m_hwnd, WM_GETFONT, 0, 0));
	HGDIOBJ oldFont = nullptr;

	if (font)
	{
		oldFont = SelectObject(hdc, font);
	}

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));

	auto separatorWidth = DpiCompatibility::GetInstance().ScaleValue(m_breadcrumbWindow, 14);
	UpdateBreadcrumbItemBounds(hdc, clientRect);

	for (size_t i = 0; i < m_breadcrumbItems.size(); i++)
	{
		const auto &item = m_breadcrumbItems[i];

		if (IsRectEmpty(&item.bounds))
		{
			break;
		}

		auto isLastItem = (i == m_breadcrumbItems.size() - 1);
		DrawBreadcrumbItem(hdc, item, item.bounds, isLastItem);

		if (!isLastItem && item.bounds.right < clientRect.right)
		{
			RECT separatorRect = { item.bounds.right, clientRect.top,
				item.bounds.right + separatorWidth,
				clientRect.bottom };
			DrawText(hdc, L">", -1, &separatorRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}

	if (oldFont)
	{
		SelectObject(hdc, oldFont);
	}

	EndPaint(m_breadcrumbWindow, &ps);
}

void AddressBarView::DrawBreadcrumbItem(HDC hdc, const BreadcrumbItem &item, const RECT &bounds,
	bool isLastItem)
{
	RECT textRect = bounds;
	auto horizontalPadding = DpiCompatibility::GetInstance().ScaleValue(m_breadcrumbWindow, 6);
	textRect.left += horizontalPadding;
	textRect.right -= horizontalPadding;

	auto drawFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

	if (isLastItem)
	{
		auto oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
		SetDCPenColor(hdc, GetSysColor(COLOR_3DLIGHT));
		MoveToEx(hdc, bounds.left, bounds.bottom - 1, nullptr);
		LineTo(hdc, bounds.right, bounds.bottom - 1);

		if (oldPen)
		{
			SelectObject(hdc, oldPen);
		}
	}

	DrawText(hdc, item.text.c_str(), -1, &textRect, drawFlags);
}

int AddressBarView::HitTestBreadcrumbItem(const POINT &pt)
{
	UpdateBreadcrumbItemBounds();

	for (size_t i = 0; i < m_breadcrumbItems.size(); i++)
	{
		if (PtInRect(&m_breadcrumbItems[i].bounds, pt))
		{
			return static_cast<int>(i);
		}
	}

	return -1;
}

void AddressBarView::OnBreadcrumbLeftButtonDown(const POINT &pt)
{
	auto itemIndex = HitTestBreadcrumbItem(pt);

	if (itemIndex == -1)
	{
		FocusTextEntry(true);
		return;
	}

	if (m_delegate)
	{
		m_delegate->OnBreadcrumbSegmentClicked(m_breadcrumbItems[itemIndex].pidl.Raw());
	}
}

void AddressBarView::OnFontOrDpiUpdated()
{
	LayoutBreadcrumbWindow();
	InvalidateRect(m_breadcrumbWindow, nullptr, TRUE);
	sizeUpdatedSignal.m_signal();
}

void AddressBarView::OnNcDestroy()
{
	windowDestroyedSignal.m_signal();

	delete this;
}

AddressBarViewDelegate *AddressBarView::GetDelegateForTesting()
{
	CHECK(IsInTest());
	return m_delegate;
}

void AddressBarView::SetTextForTesting(const std::wstring &text)
{
	CHECK(IsInTest());
	FocusTextEntry(false);
	SendMessage(m_hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
	SendMessage(GetEditControl(), EM_SETMODIFY, TRUE, 0);
}

size_t AddressBarView::GetBreadcrumbSegmentCountForTesting() const
{
	CHECK(IsInTest());
	return m_breadcrumbItems.size();
}
