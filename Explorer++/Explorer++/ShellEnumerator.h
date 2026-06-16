// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "ShellItemFilter.h"
#include "../Helper/Pidl.h"
#include <functional>
#include <stop_token>
#include <vector>

using ShellEnumeratorItemBatchCallback = std::function<void(std::vector<PidlChild> items)>;

class ShellEnumerator
{
public:
	virtual ~ShellEnumerator() = default;

	virtual HRESULT EnumerateDirectory(PCIDLIST_ABSOLUTE pidlDirectory,
		ShellItemFilter::ItemType itemType, ShellItemFilter::HiddenItemPolicy hiddenItemPolicy,
		std::vector<PidlChild> &outputItems, std::stop_token stopToken,
		ShellEnumeratorItemBatchCallback itemBatchCallback = nullptr) const = 0;
};
