/*
 * (C) 2018-2025 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "IVideoRenderer.h"
#include "PageFlip.h"
#include "../Include/FilterInterfaces.h"

// CVRMainPPage

class __declspec(uuid("DA46D181-07D6-441D-B314-019AEB10148A"))
	CVRMainPPage : public CBasePropertyPage, public CWindow
{
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;

	Settings_t m_SetsPP;

	int m_oldSDRDisplayNits = SDR_NITS_DEF;

	HWND m_hHint = nullptr;

public:
	CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRMainPPage();

private:
	void SetControls();
	void EnableControls();

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	HRESULT OnApplyChanges() override;

	HWND CreateHintWindow(HWND parent, int timePop = 1700, int timeInit = 70, int timeReshow = 7);
	void AddHint(int id, const LPCWSTR text);
};

// CVRPageFlipPPage

class __declspec(uuid("4E2A5682-6D2F-4DC7-AE7A-F3B7E4B6D2A6"))
	CVRPageFlipPPage : public CBasePropertyPage, public CWindow
{
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;
	CComQIPtr<IExFilterConfig> m_pFilterConfig;
	PageFlipConfig m_cfg;
	std::vector<PageFlipPortInfo> m_ports;
	std::wstring m_configPath;
	std::wstring m_missingPort;
	LocalEmitterSettings m_localSettings;
	bool m_loading = false;
	bool m_connected = false;
	bool m_emitterPending = false;
	bool m_emitterDirty = false;
	int m_firmwareVersion = 0;
	HBRUSH m_warnBrush = nullptr;
	HWND m_hHint = nullptr;
	bool m_targetWarnRed = false;

public:
	CVRPageFlipPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRPageFlipPPage();

private:
	void SetControls();
	PageFlipConfig GetConfigFromControls() const;
	void PopulatePorts(const std::wstring& selectedPort);
	std::wstring GetSelectedPort() const;
	void UpdateEmitterState();
	void UpdateLocalEmitterSettings();
	void ApplyLocalEmitterSettings(bool disableAutoConnect, const std::wstring& comPort);
	void UpdateModeControls();
	void UpdateEmitterDirty();
	void UpdateTimingWarnings(const PageFlipConfig& cfg);
	void SetOpticalControlsEnabled(bool enabled);
	HWND CreateHintWindow(HWND parent, int timePop = 15000, int timeInit = 70, int timeReshow = 7);
	void AddHint(int id, const LPCWSTR text);
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	HRESULT OnApplyChanges() override;
};

// CVREmitterPPage

class __declspec(uuid("F6E9E5F3-7C93-4762-9AE8-8E1E8CC6E9F2"))
	CVREmitterPPage : public CBasePropertyPage, public CWindow
{
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;
	CComQIPtr<IExFilterConfig> m_pFilterConfig;
	PageFlipConfig m_cfg;
	std::vector<PageFlipPortInfo> m_ports;
	std::wstring m_configPath;
	std::wstring m_missingPort;
	LocalEmitterSettings m_localSettings;
	bool m_loading = false;
	bool m_connected = false;
	int m_firmwareVersion = 0;
	HWND m_hHint = nullptr;

public:
	CVREmitterPPage(LPUNKNOWN lpunk, HRESULT* phr);

private:
	void SetControls();
	void PopulatePorts(const std::wstring& selectedPort);
	std::wstring GetSelectedPort() const;
	PageFlipConfig GetConfigFromControls() const;
	void UpdateEmitterState();
	void UpdateLocalEmitterSettings();
	void ApplyLocalEmitterSettings(bool disableAutoConnect, const std::wstring& comPort);
	HWND CreateHintWindow(HWND parent, int timePop = 15000, int timeInit = 70, int timeReshow = 7);
	void AddHint(int id, const LPCWSTR text);
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	HRESULT OnApplyChanges() override;
};

// CVRInfoPPage

class __declspec(uuid("D697132B-FCA4-4401-8869-D3B39D0750DB"))
	CVRInfoPPage : public CBasePropertyPage, public CWindow
{
	HFONT m_hMonoFont = nullptr;
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;

public:
	CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRInfoPPage();

private:
	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
};
