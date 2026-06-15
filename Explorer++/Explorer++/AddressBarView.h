// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "MainFontSetter.h"
#include "../Helper/Pidl.h"
#include "../Helper/SignalWrapper.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class AddressBarViewDelegate;
struct Config;
class WindowSubclass;

class AddressBarView
{
public:
	struct BreadcrumbSegment
	{
		std::wstring text;
		PidlAbsolute pidl;
	};

	// Signals
	SignalWrapper<AddressBarView, void()> sizeUpdatedSignal;
	SignalWrapper<AddressBarView, void()> windowDestroyedSignal;

	static AddressBarView *Create(HWND parent, const Config *config);

	void SetDelegate(AddressBarViewDelegate *delegate);
	HWND GetHWND() const;
	std::wstring GetText() const;
	bool IsTextModified() const;
	void SelectAllText();
	void UpdateTextAndIcon(const std::optional<std::wstring> &optionalText, int iconIndex);
	void UpdateTextAndIcon(const std::optional<std::wstring> &optionalText, int iconIndex,
		std::optional<std::vector<BreadcrumbSegment>> breadcrumbSegments);
	void RevertText();
	void ShowBreadcrumb();
	void FocusTextEntry(bool selectText);

	AddressBarViewDelegate *GetDelegateForTesting();
	void SetTextForTesting(const std::wstring &text);
	size_t GetBreadcrumbSegmentCountForTesting() const;

private:
	struct BreadcrumbItem
	{
		std::wstring text;
		PidlAbsolute pidl;
		RECT bounds = {};
	};

	AddressBarView(HWND parent, const Config *config);

	static HWND CreateAddressBar(HWND parent);
	static HWND CreateBreadcrumbWindow(HWND parent);

	LRESULT ComboBoxExSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT BreadcrumbSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT EditSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT ParentSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	HWND GetEditControl() const;
	void SetBreadcrumbSegments(const std::vector<BreadcrumbSegment> &segments);
	void LayoutBreadcrumbWindow();
	void SetEditMode(bool editMode, bool selectText);
	void UpdateBreadcrumbItemBounds();
	void UpdateBreadcrumbItemBounds(HDC hdc, const RECT &clientRect);
	void OnBreadcrumbPaint();
	void DrawBreadcrumbItem(HDC hdc, const BreadcrumbItem &item, const RECT &bounds,
		bool isLastItem);
	int HitTestBreadcrumbItem(const POINT &pt);
	void OnBreadcrumbLeftButtonDown(const POINT &pt);
	void OnFontOrDpiUpdated();
	void OnNcDestroy();

	const HWND m_hwnd;
	const HWND m_breadcrumbWindow;
	AddressBarViewDelegate *m_delegate = nullptr;
	MainFontSetter m_fontSetter;
	std::wstring m_currentText;
	std::vector<BreadcrumbItem> m_breadcrumbItems;

	std::vector<std::unique_ptr<WindowSubclass>> m_windowSubclasses;
};
