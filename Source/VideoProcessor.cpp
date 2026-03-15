/*
 * (C) 2020-2025 see Authors.txt
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

#include "stdafx.h"

#include <cmath>
#include <limits>
#include <Mferror.h>
#include "Helper.h"
#include "Times.h"
#include "VideoRenderer.h"

#include "VideoProcessor.h"
#include <shellscalingapi.h>

static std::wstring GetPageFlipConfigFolder(const std::wstring& configPath)
{
	const size_t pos = configPath.find_last_of(L"\\/");
	if (pos == std::wstring::npos) {
		return L".";
	}
	return configPath.substr(0, pos);
}

HRESULT CVideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	std::scoped_lock lock(m_pageFlipStateMutex);
	UINT srcWidth = m_srcRectWidth;
	UINT srcHeight = m_srcRectHeight;
	if (m_pageFlipConfig.enabled && m_pageFlipLayout != PageFlipLayout::None && m_pageFlipViewWidth && m_pageFlipViewHeight) {
		srcWidth = m_pageFlipViewWidth;
		srcHeight = m_pageFlipViewHeight;
	}

	if (m_iRotation == 90 || m_iRotation == 270) {
		*pWidth  = srcHeight;
		*pHeight = srcWidth;
	} else {
		*pWidth  = srcWidth;
		*pHeight = srcHeight;
	}

	return S_OK;
}

HRESULT CVideoProcessor::GetAspectRatio(long *plAspectX, long *plAspectY)
{
	CheckPointer(plAspectX, E_POINTER);
	CheckPointer(plAspectY, E_POINTER);

	std::scoped_lock lock(m_pageFlipStateMutex);
	DWORD aspectX = m_srcAspectRatioX;
	DWORD aspectY = m_srcAspectRatioY;
	if (m_pageFlipConfig.enabled && m_pageFlipLayout != PageFlipLayout::None && m_pageFlipAspectRatioX && m_pageFlipAspectRatioY) {
		aspectX = m_pageFlipAspectRatioX;
		aspectY = m_pageFlipAspectRatioY;
	}

	if (m_iRotation == 90 || m_iRotation == 270) {
		*plAspectX = aspectY;
		*plAspectY = aspectX;
	} else {
		*plAspectX = aspectX;
		*plAspectY = aspectY;
	}

	return S_OK;
}

CVideoProcessor::~CVideoProcessor()
{
	StopPageFlipThread();
	m_pageFlipSerial.Stop();
	m_nvidiaVision.Stop();
	if (m_pageFlipWakeEvent) {
		CloseHandle(m_pageFlipWakeEvent);
		m_pageFlipWakeEvent = nullptr;
	}
}

void CVideoProcessor::InitPageFlip()
{
	m_pageFlipConfigPath = GetDefaultPageFlipConfigPath();
	m_pageFlipLocalEmitterPath = GetLocalEmitterSettingsPath(m_pageFlipConfigPath);
	m_pageFlipLogPath = GetPageFlipLogPath(m_pageFlipConfigPath);
	m_pageFlipWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	m_pageFlipLocalEmitterSettings = LoadLocalEmitterSettings(m_pageFlipConfigPath);

	// Load NVIDIA 3D Vision timing profiles from config folder
	const std::wstring timingPath = GetPageFlipConfigFolder(m_pageFlipConfigPath) + L"\\MonitorTimings.ini";
	m_nvidiaVision.LoadTimingProfiles(timingPath);

	PageFlipConfig cfg = LoadPageFlipConfig(m_pageFlipConfigPath);
	cfg.comPort = m_pageFlipLocalEmitterSettings.comPort;
	ApplyPageFlipConfig(cfg, true);
}

void CVideoProcessor::ReloadPageFlipConfig(bool force)
{
	PageFlipConfig cfg = LoadPageFlipConfig(m_pageFlipConfigPath);
	ApplyPageFlipConfig(cfg, force);
}

void CVideoProcessor::ApplyPageFlipConfig(const PageFlipConfig& config, bool force)
{
	PageFlipConfig normalized = config;
	normalized.comPort = m_pageFlipLocalEmitterSettings.comPort;
	m_pageFlipLogger.Configure(m_pageFlipLogPath, normalized.logLevel);

	PageFlipConfig cfg;
	bool calibrationExit = false;
	bool needVideoRectSizeUpdate = false;
	{
		std::scoped_lock lock(m_pageFlipStateMutex);
		const PageFlipConfig oldCfg = m_pageFlipConfig;
		const bool wasCalibration = m_pageFlipConfig.calibrationMode;
		if (wasCalibration && !normalized.calibrationMode) {
			calibrationExit = true;
		}
		if (!force && normalized == m_pageFlipConfig) {
			return;
		}

		m_pageFlipConfig = normalized;
		needVideoRectSizeUpdate =
			(oldCfg.enabled != normalized.enabled)
			|| (oldCfg.defaultAspect != normalized.defaultAspect)
			|| (oldCfg.displayZoomFactor != normalized.displayZoomFactor);
		UpdatePageFlipLayout();
		UpdatePageFlipRate();
		cfg = m_pageFlipConfig;
	}

	m_pageFlipSerial.Start(cfg, &m_pageFlipLogger, m_pageFlipLocalEmitterSettings.disableAutoConnect);
	if (cfg.enabled && cfg.irDriveMode == 2) {
		m_nvidiaVision.Start(&m_pageFlipLogger);
	} else {
		m_nvidiaVision.Stop();
	}
	if (cfg.enabled) {
		EnsurePageFlipThread();
	} else {
		m_pageFlipHasFrame = false;
		StopPageFlipThread();
	}
	if (calibrationExit) {
		m_pageFlipSerial.SetOptDebugLogging(false, GetPageFlipConfigFolder(m_pageFlipConfigPath), nullptr);
	}

	m_pageFlipLogger.Log(PageFlipLogLevel::Info, L"Pageflip config: enabled={}, rateHz={}, aspect={}, flipEyes={}, overlay={}, comPort='{}'.",
		cfg.enabled ? 1 : 0,
		cfg.rateHz,
		cfg.defaultAspect == PageFlipAspectMode::SideBySide ? L"sbs" : L"tab",
		cfg.flipEyes ? 1 : 0,
		cfg.showOverlay ? 1 : 0,
		cfg.comPort);

	if (m_pFilter) {
		if (needVideoRectSizeUpdate) {
			m_pFilter->UpdateVideoRectForPageFlip();
			m_pFilter->UpdateVideoSizeForPageFlip();
		}
		m_pFilter->UpdateRawInputRegistration();
	}
}

void CVideoProcessor::SetPageFlipConfig(const PageFlipConfig& config, bool force)
{
	ApplyPageFlipConfig(config, force);
}

PageFlipConfig CVideoProcessor::GetPageFlipConfig() const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	return m_pageFlipConfig;
}

bool CVideoProcessor::IsPageFlipEnabled() const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	return m_pageFlipConfig.enabled;
}

bool CVideoProcessor::ShouldDrawPageFlipCalibrationText() const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	return m_pageFlipConfig.enabled && m_pageFlipConfig.calibrationMode;
}

void CVideoProcessor::SetLocalEmitterSettings(const LocalEmitterSettings& settings, bool persist)
{
	m_pageFlipLocalEmitterSettings = settings;

	PageFlipConfig updated;
	{
		std::scoped_lock lock(m_pageFlipStateMutex);
		updated = m_pageFlipConfig;
		updated.comPort = settings.comPort;
		m_pageFlipConfig.comPort = settings.comPort;
	}

	m_pageFlipSerial.SetAutoConnectDisabled(settings.disableAutoConnect);
	m_pageFlipSerial.UpdateConfig(updated);

	if (persist) {
		SaveLocalEmitterSettings(m_pageFlipConfigPath, settings);
	}
}

bool CVideoProcessor::SetPageFlipEmitterConnected(bool connected)
{
	m_pageFlipSerial.SetUserConnected(connected);
	return m_pageFlipSerial.IsConnected();
}

bool CVideoProcessor::RefreshPageFlipEmitter(PageFlipEmitterState& state, bool clearDirty)
{
	PageFlipConfig updated = GetPageFlipConfig();
	bool ok = m_pageFlipSerial.ReadEmitterSettings(updated);
	if (ok) {
		ApplyPageFlipConfig(updated, true);
		if (clearDirty) {
			SetPageFlipEmitterDirty(false);
		}
	}
	state.config = GetPageFlipConfig();
	state.firmwareVersion = m_pageFlipSerial.GetFirmwareVersion();
	state.connected = m_pageFlipSerial.IsConnected();
	return ok;
}

bool CVideoProcessor::ApplyPageFlipEmitterSettings(const PageFlipConfig& config)
{
	PageFlipConfig updated = GetPageFlipConfig();
	updated.comPort = config.comPort;
	updated.irDriveMode = config.irDriveMode;
	updated.irProtocol = config.irProtocol;
	updated.irFrameDelay = config.irFrameDelay;
	updated.irFrameDuration = config.irFrameDuration;
	updated.irSignalSpacing = config.irSignalSpacing;
	updated.irFlipEyes = config.irFlipEyes;
	updated.irAverageTimingMode = config.irAverageTimingMode;
	updated.targetFrametime = config.targetFrametime;
	updated.optBlockSignalDetectionDelay = config.optBlockSignalDetectionDelay;
	updated.optIgnoreAllDuplicates = config.optIgnoreAllDuplicates;
	updated.optSensorFilterMode = config.optSensorFilterMode;
	updated.optMinThresholdValueToActivate = config.optMinThresholdValueToActivate;
	updated.optDetectionThresholdHigh = config.optDetectionThresholdHigh;
	updated.optDetectionThresholdLow = config.optDetectionThresholdLow;
	updated.optEnableIgnoreDuringIr = config.optEnableIgnoreDuringIr;
	updated.optEnableDuplicateRealtimeReporting = config.optEnableDuplicateRealtimeReporting;
	updated.optOutputStats = config.optOutputStats;

	const bool ok = m_pageFlipSerial.ApplyEmitterSettings(updated);
	if (ok) {
		ApplyPageFlipConfig(updated, true);
		SetPageFlipEmitterDirty(true);
	}
	return ok;
}

bool CVideoProcessor::SavePageFlipEmitterSettings()
{
	const bool ok = m_pageFlipSerial.SaveEmitterSettingsToEeprom();
	if (ok) {
		SetPageFlipEmitterDirty(false);
	}
	return ok;
}

PageFlipEmitterState CVideoProcessor::GetPageFlipEmitterState() const
{
	PageFlipEmitterState state = {};
	state.config = GetPageFlipConfig();
	state.firmwareVersion = m_pageFlipSerial.GetFirmwareVersion();
	state.connected = m_pageFlipSerial.IsConnected();
	return state;
}

void CVideoProcessor::UpdatePageFlipLayout()
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	m_pageFlipLoggedProcess = false;

	if (!m_pageFlipConfig.enabled || !m_srcRectWidth || !m_srcRectHeight) {
		m_pageFlipLayout = PageFlipLayout::None;
		m_pageFlipViewWidth = 0;
		m_pageFlipViewHeight = 0;
		m_pageFlipAspectRatioX = 0;
		m_pageFlipAspectRatioY = 0;
		return;
	}

	constexpr double kAspectMatchTolerance = 0.01;
	auto AspectDiff = [](DWORD aspectX, DWORD aspectY, UINT width, UINT height) -> double {
		if (!aspectX || !aspectY || !width || !height) {
			return std::numeric_limits<double>::infinity();
		}
		const double left = static_cast<double>(aspectX) * height;
		const double right = static_cast<double>(aspectY) * width;
		if (right == 0.0) {
			return std::numeric_limits<double>::infinity();
		}
		return std::abs(left - right) / right;
	};

	const UINT refWidth = m_srcRectWidth;
	const UINT refHeight = m_srcRectHeight;
	const double packedDiff = AspectDiff(m_srcAspectRatioX, m_srcAspectRatioY, refWidth, refHeight);
	const double eyeSbsDiff = AspectDiff(m_srcAspectRatioX, m_srcAspectRatioY, refWidth / 2, refHeight);
	const double eyeTabDiff = AspectDiff(m_srcAspectRatioX, m_srcAspectRatioY, refWidth, refHeight / 2);
	const bool packedAspect = packedDiff <= kAspectMatchTolerance;
	const bool eyeAspectSbs = eyeSbsDiff <= kAspectMatchTolerance;
	const bool eyeAspectTab = eyeTabDiff <= kAspectMatchTolerance;

	const double ar = (double)m_srcRectWidth / (double)m_srcRectHeight;
	if (ar > 2.39) {
		m_pageFlipLayout = PageFlipLayout::SideBySideFull;
	} else if (ar < (4.0 / 3.0)) {
		m_pageFlipLayout = PageFlipLayout::TopAndBottomFull;
	} else {
		m_pageFlipLayout = (m_pageFlipConfig.defaultAspect == PageFlipAspectMode::SideBySide)
			? PageFlipLayout::SideBySideHalf
			: PageFlipLayout::TopAndBottomHalf;
	}

	if (m_pageFlipLayout == PageFlipLayout::SideBySideFull || m_pageFlipLayout == PageFlipLayout::SideBySideHalf) {
		if (m_pageFlipLayout == PageFlipLayout::SideBySideFull) {
			m_pageFlipViewWidth = std::max(1u, m_srcRectWidth / 2);
		} else {
			m_pageFlipViewWidth = std::max(1u, m_srcRectWidth);
		}
		m_pageFlipViewHeight = std::max(1u, m_srcRectHeight);
		m_pageFlipAspectRatioX = m_srcAspectRatioX;
		m_pageFlipAspectRatioY = m_srcAspectRatioY;
		if (m_pageFlipLayout == PageFlipLayout::SideBySideFull && (packedAspect || packedDiff <= eyeSbsDiff) && !eyeAspectSbs) {
			if (m_srcAspectRatioY <= (std::numeric_limits<DWORD>::max)() / 2) {
				m_pageFlipAspectRatioY = m_srcAspectRatioY * 2;
			}
		}
	} else if (m_pageFlipLayout == PageFlipLayout::TopAndBottomFull || m_pageFlipLayout == PageFlipLayout::TopAndBottomHalf) {
		m_pageFlipViewWidth = std::max(1u, m_srcRectWidth);
		if (m_pageFlipLayout == PageFlipLayout::TopAndBottomFull) {
			m_pageFlipViewHeight = std::max(1u, m_srcRectHeight / 2);
		} else {
			m_pageFlipViewHeight = std::max(1u, m_srcRectHeight);
		}
		m_pageFlipAspectRatioX = m_srcAspectRatioX;
		m_pageFlipAspectRatioY = m_srcAspectRatioY;
		if (m_pageFlipLayout == PageFlipLayout::TopAndBottomFull && (packedAspect || packedDiff <= eyeTabDiff) && !eyeAspectTab) {
			if (m_srcAspectRatioX <= (std::numeric_limits<DWORD>::max)() / 2) {
				m_pageFlipAspectRatioX = m_srcAspectRatioX * 2;
			}
		}
	}

	if (m_pageFlipAspectRatioX && m_pageFlipAspectRatioY) {
		const auto ar_gcd = std::gcd(m_pageFlipAspectRatioX, m_pageFlipAspectRatioY);
		if (ar_gcd) {
			m_pageFlipAspectRatioX /= ar_gcd;
			m_pageFlipAspectRatioY /= ar_gcd;
		}
	}

	m_pageFlipLogger.Log(PageFlipLogLevel::Debug,
		L"Pageflip layout: layout={}, srcRect={}x{}, view={}x{}, srcAR={}:{}, pfAR={}:{}, packedDiff={:.4f}, eyeSbsDiff={:.4f}, eyeTabDiff={:.4f}",
		static_cast<int>(m_pageFlipLayout),
		m_srcRectWidth, m_srcRectHeight,
		m_pageFlipViewWidth, m_pageFlipViewHeight,
		m_srcAspectRatioX, m_srcAspectRatioY,
		m_pageFlipAspectRatioX, m_pageFlipAspectRatioY,
		packedDiff, eyeSbsDiff, eyeTabDiff);

	ResetPageFlipState();
}

void CVideoProcessor::UpdatePageFlipRate()
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	const double prevRate = m_pageFlipResolvedRateHz;
	double rate = m_pageFlipConfig.rateHz;
	if (rate <= 0.0 && m_pFilter && m_pFilter->m_DisplayConfig.refreshRate.Numerator) {
		rate = (double)m_pFilter->m_DisplayConfig.refreshRate.Numerator
			/ (double)m_pFilter->m_DisplayConfig.refreshRate.Denominator;
	}
	if (rate <= 0.0) {
		rate = 120.0;
	}

	m_pageFlipResolvedRateHz = rate;
	m_pageFlipPeriodTicks = (uint64_t)std::llround(GetPreciseTicksPerSecond() / rate);
	if (!m_pageFlipPeriodTicks) {
		m_pageFlipPeriodTicks = 1;
	}
	m_pageFlipLateThresholdTicks = (m_pageFlipPeriodTicks * 3) / 2;

	ResetPageFlipState();

	if (m_pageFlipConfig.enabled && (prevRate != m_pageFlipResolvedRateHz)) {
		m_pageFlipLogger.Log(PageFlipLogLevel::Info, L"Pageflip rate resolved to {:.3f} Hz.", m_pageFlipResolvedRateHz);
	}
}

void CVideoProcessor::ResetPageFlipState()
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	m_pageFlipEye = 0;
	m_pageFlipNextTick = 0;
	m_pageFlipLastPresentTick = 0;
	m_pageFlipSkipVBlank = false;
}

void CVideoProcessor::EnsurePageFlipThread()
{
	{
		std::scoped_lock lock(m_pageFlipStateMutex);
		if (!m_pageFlipConfig.enabled || m_pageFlipThreadRunning.load()) {
			return;
		}
	}

	m_pageFlipStopRequested = false;
	m_pageFlipThreadRunning = true;
	try {
		m_pageFlipThread = std::thread(&CVideoProcessor::PageFlipThreadProc, this);
	} catch (...) {
		m_pageFlipThreadRunning = false;
		m_pageFlipLogger.Log(PageFlipLogLevel::Error, L"Pageflip: failed to start thread.");
	}
}

void CVideoProcessor::StopPageFlipThread()
{
	if (!m_pageFlipThreadRunning.load()) {
		return;
	}

	m_pageFlipStopRequested = true;
	if (m_pageFlipWakeEvent) {
		SetEvent(m_pageFlipWakeEvent);
	}
	if (m_pageFlipThread.joinable()) {
		if (m_pageFlipThread.get_id() == std::this_thread::get_id()) {
			return;
		}
		m_pageFlipThread.join();
	}
	m_pageFlipThreadRunning = false;
	m_pageFlipStopRequested = false;

	// Apply any pending staged frame so normal rendering path has valid data
	ApplyPageFlipStagedFrame();
}

void CVideoProcessor::PageFlipThreadProc()
{
	SetThreadName(DWORD(-1), "PageFlipThread");
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	const uint64_t ticksPerSecond = GetPreciseTicksPerSecondI();

	for (;;) {
		if (m_pageFlipStopRequested.load()) {
			break;
		}

		PageFlipConfig cfg;
		PageFlipLayout layout = PageFlipLayout::None;
		uint64_t periodTicks = 0;
		uint64_t lateThreshold = 0;
		uint64_t nextTick = 0;
		{
			std::scoped_lock lock(m_pageFlipStateMutex);
			cfg = m_pageFlipConfig;
			layout = m_pageFlipLayout;
			periodTicks = m_pageFlipPeriodTicks;
			lateThreshold = m_pageFlipLateThresholdTicks;
			nextTick = m_pageFlipNextTick;
		}

		if (!cfg.enabled || layout == PageFlipLayout::None || !m_pageFlipHasFrame.load()) {
			if (m_pageFlipWakeEvent) {
				WaitForSingleObject(m_pageFlipWakeEvent, 10);
			} else {
				Sleep(10);
			}
			continue;
		}

		const uint64_t now = GetPreciseTick();
		if (!nextTick) {
			nextTick = now + periodTicks;
			{
				std::scoped_lock lock(m_pageFlipStateMutex);
				m_pageFlipNextTick = nextTick;
			}
		}

		if (now < nextTick) {
			uint64_t waitTicks = nextTick - now;
			DWORD waitMs = (DWORD)std::clamp<uint64_t>(waitTicks * 1000 / ticksPerSecond, 1, 50);
			{
				std::scoped_lock lock(m_pageFlipStateMutex);
				m_pageFlipNextTick = nextTick;
			}
			if (m_pageFlipWakeEvent) {
				WaitForSingleObject(m_pageFlipWakeEvent, waitMs);
			} else {
				Sleep(waitMs);
			}
			continue;
		}

		bool late = false;
		const uint64_t last = m_pageFlipLastPresentTick.load();
		if (last && (now - last) > lateThreshold) {
			late = true;
		}

		const int eyeValue = m_pageFlipEye.load();
		const PageFlipEye eye = static_cast<PageFlipEye>(eyeValue);

		bool waited = WaitForVBlank();
		if (m_pageFlipStopRequested.load()) {
			break;
		}
		m_pageFlipSkipVBlank = waited;
		if (late) {
			m_pageFlipLogger.Log(PageFlipLogLevel::Warning, L"Pageflip: LATE frame, eye={} (not toggling)", eyeValue);
		}
		m_pageFlipSerial.QueueSignal(eye);
		m_nvidiaVision.QueueSignal(eye);

		// Double-buffering: on LEFT eye, apply any pending staged frame before rendering
		// This ensures the first frame is visible and both eyes see the same data
		if (eyeValue == 0) {
			ApplyPageFlipStagedFrame();
		}

		{
			CAutoLock cRendererLock(&m_pFilter->m_RendererLock);
			if (m_pFilter->m_filterState != State_Stopped && m_pFilter->m_bValidBuffer) {
				Render(0, INVALID_TIME);
			}
		}
		m_pageFlipSkipVBlank = false;
		m_pageFlipLastPresentTick = GetPreciseTick();

		if (!late) {
			m_pageFlipEye.store(eyeValue ? 0 : 1);
		}

		// Double-buffering: apply staged frame in safe window
		// After RIGHT eye (eyeValue==1) or during late recovery (safe since no active pair)
		if (eyeValue == 1 || late) {
			ApplyPageFlipStagedFrame();
		}

		if (late) {
			nextTick = now + periodTicks;
		} else {
			nextTick += periodTicks;
		}

		{
			std::scoped_lock lock(m_pageFlipStateMutex);
			m_pageFlipNextTick = nextTick;
		}
	}

	// Apply any remaining staged frame before exiting
	ApplyPageFlipStagedFrame();

	m_pageFlipThreadRunning = false;
}

bool CVideoProcessor::GetPageFlipSrcRect(const CRect& srcRect, CRect& outRect) const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	if (!m_pageFlipConfig.enabled || m_pageFlipLayout == PageFlipLayout::None) {
		return false;
	}

	outRect = srcRect;

	const int width = outRect.Width();
	const int height = outRect.Height();
	int eye = m_pageFlipEye.load();
	if (m_pageFlipConfig.flipEyes) {
		eye = eye ? 0 : 1;
	}

	if (m_pageFlipLayout == PageFlipLayout::SideBySideFull || m_pageFlipLayout == PageFlipLayout::SideBySideHalf) {
		const int half = width / 2;
		if (eye == 0) {
			outRect.right = outRect.left + half;
		} else {
			outRect.left = outRect.right - half;
		}
		return true;
	}

	if (m_pageFlipLayout == PageFlipLayout::TopAndBottomFull || m_pageFlipLayout == PageFlipLayout::TopAndBottomHalf) {
		const int half = height / 2;
		if (eye == 0) {
			outRect.bottom = outRect.top + half;
		} else {
			outRect.top = outRect.bottom - half;
		}
		return true;
	}

	return false;
}

bool CVideoProcessor::GetPageFlipDstRect(const CRect& dstRect, CRect& outRect) const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	if (!m_pageFlipConfig.enabled || m_pageFlipLayout == PageFlipLayout::None) {
		return false;
	}

	outRect = dstRect;
	const int width = outRect.Width();
	if (width <= 0) {
		return false;
	}

	const double parallaxPct = m_pageFlipConfig.displayParallax;
	if (parallaxPct == 0.0) {
		return false;
	}

	const int offset = (int)std::lround((width * parallaxPct) / 100.0);
	if (!offset) {
		return false;
	}
	if (std::abs(offset) > (width / 4)) {
		return false;
	}

	int eye = m_pageFlipEye.load();
	if (m_pageFlipConfig.flipEyes) {
		eye = eye ? 0 : 1;
	}

	const int eyeOffset = (eye == 0) ? -offset : offset;
	outRect.OffsetRect(eyeOffset, 0);
	return true;
}

std::wstring CVideoProcessor::GetPageFlipStatusText() const
{
	if (!ShouldDrawPageFlipStatusText()) {
		return {};
	}

	std::scoped_lock lock(m_pageFlipStateMutex);
	const bool messageActive = IsPageFlipSerialMessageActive();
	if (!m_pageFlipConfig.showOverlay && messageActive) {
		return m_pageFlipSerialMessage;
	}

	const wchar_t* layout = L"none";
	switch (m_pageFlipLayout) {
	case PageFlipLayout::SideBySideFull: layout = L"sbs-full"; break;
	case PageFlipLayout::SideBySideHalf: layout = L"sbs-half"; break;
	case PageFlipLayout::TopAndBottomFull: layout = L"tab-full"; break;
	case PageFlipLayout::TopAndBottomHalf: layout = L"tab-half"; break;
	default: break;
	}

	int eye = m_pageFlipEye.load();
	if (m_pageFlipConfig.flipEyes) {
		eye = eye ? 0 : 1;
	}
	const wchar_t* eyeStr = eye ? L"R" : L"L";

	const wchar_t* rateMode = (m_pageFlipConfig.rateHz <= 0.0) ? L"auto" : L"fixed";
	const std::wstring comPort = m_pageFlipConfig.comPort.empty() ? L"auto" : m_pageFlipConfig.comPort;

	int driveModeValue = 0;
	if (m_nvidiaVision.IsConnected()) {
		driveModeValue = 2;
	} else if (m_pageFlipSerial.IsConnected()) {
		driveModeValue = m_pageFlipConfig.irDriveMode;
	}
	const wchar_t* driveMode = (driveModeValue == 2) ? L"nvidia_vision" : (driveModeValue == 1) ? L"serial" : L"optical";

	std::wstring text = std::format(L"Pageflip: {} {} {:.3f} Hz {} drive={} COM={} zoom={}% par={:.3g}%",
		eyeStr,
		layout,
		m_pageFlipResolvedRateHz,
		rateMode,
		driveMode,
		comPort,
		m_pageFlipConfig.displayZoomFactor,
		m_pageFlipConfig.displayParallax);

	if (m_pageFlipConfig.flipEyes) {
		text.append(L" flip");
	}

	text.append(L"\nCtrl+Shift+F# Hotkeys: F8=2d/3d  F9=osd  F10=calibration mode  F11=open properties  F12=flip eyes");
	if (messageActive && !m_pageFlipSerialMessage.empty()) {
		text.append(L"\n").append(m_pageFlipSerialMessage);
	}
	if (IsPageFlipEmitterDirty()) {
		text.append(L"\nEmitter settings not saved to EEPROM (press B to save)");
	}

	const int inputW = static_cast<int>(m_srcWidth);
	const int inputH = static_cast<int>(m_srcHeight);
	const int videoW = std::max(0, m_videoRect.Width());
	const int videoH = std::max(0, m_videoRect.Height());
	const int renderW = std::max(0, m_renderRect.Width());
	const int renderH = std::max(0, m_renderRect.Height());

	text.append(std::format(L"\nIn {}x{}  Vid {}x{}  Ren {}x{}", inputW, inputH, videoW, videoH, renderW, renderH));
	text.append(std::format(L"\nSrcRect {}x{}  SrcAR {}:{}  PFAR {}:{}",
		m_srcRectWidth, m_srcRectHeight,
		m_srcAspectRatioX, m_srcAspectRatioY,
		m_pageFlipAspectRatioX, m_pageFlipAspectRatioY));

	return text;
}

std::wstring CVideoProcessor::GetPageFlipCalibrationText() const
{
	if (!ShouldDrawPageFlipCalibrationText()) {
		return {};
	}

	std::scoped_lock lock(m_pageFlipStateMutex);
	std::wstring text;
	int driveModeValue = 0;
	if (m_nvidiaVision.IsConnected()) {
		driveModeValue = 2;
	} else if (m_pageFlipSerial.IsConnected()) {
		driveModeValue = m_pageFlipConfig.irDriveMode;
	}
	const bool serialMode = driveModeValue == 1;
	const bool nvidiaMode = driveModeValue == 2;
	const uint64_t now = GetPreciseTick();
	const uint64_t showWindow = GetPreciseTicksPerSecondI() * 2;
	if (!m_pageFlipCalibrationMessage.empty()
		&& (now - m_pageFlipCalibrationMessageTick) < showWindow) {
		text.append(m_pageFlipCalibrationMessage);
	}
	if (m_pageFlipShowCalibrationHelp) {
		auto appendLine = [&](const wchar_t* line) {
			if (!text.empty()) {
				text.append(L"\n");
			}
			text.append(line);
		};
		appendLine(L"G: Toggle calibration help overlay.");
		appendLine(L"T: Toggle drive mode (0=optical, 1=serial, 2=nvidia_vision).");
		appendLine(L"B: Save current emitter settings to EEPROM.");
		appendLine(L"I/K: (us) Delay after signal before activating glasses.");
		appendLine(L"O/L: (us) Duration to keep glasses active after activation.");
		appendLine(L"Shift: Use larger step sizes for adjustments.");
		if (!serialMode) {
			appendLine(L"W/S: Whitebox vertical position; roughly mm when display size is correct.");
			appendLine(L"A/D: Whitebox horizontal position; roughly mm when display size is correct.");
			appendLine(L"Q/E: Spacing between the two whiteboxes; roughly mm when display size is correct.");
			appendLine(L"Z/X: Whitebox size; too large causes crosstalk, too small misses triggers.");
			appendLine(L"N/M: Width of black border that blocks video content from the trigger boxes.");
			appendLine(L"P: Toggle optical debug logging.");
		}
		const wchar_t* driveModeStr = nvidiaMode ? L"nvidia_vision" : (serialMode ? L"serial" : L"optical");
		const std::wstring driveLine = std::format(L"Drive mode: {}", driveModeStr);
		appendLine(driveLine.c_str());
		appendLine(L"Ctrl+Shift+F# Hotkeys: F8=2d/3d  F9=osd  F10=calibration mode  F11=open properties  F12=flip eyes");
	 }

	if (m_pageFlipShowCalibrationHelp) {
		if (!text.empty()) {
			text.append(L"\n");
		}
		if (serialMode || nvidiaMode) {
			text.append(std::format(L"Opt debug logging: N/A ({} mode)", nvidiaMode ? L"nvidia_vision" : L"serial"));
		} else {
			const bool optLogging = m_pageFlipSerial.IsOptDebugLogging();
			text.append(std::format(L"Opt debug logging: {}", optLogging ? L"ON" : L"OFF"));
		}
	}
	if (IsPageFlipEmitterDirty()) {
		if (!text.empty()) {
			text.append(L"\n");
		}
		text.append(L"Emitter settings not saved to EEPROM (press B to save)");
	}

	return text;
}

bool CVideoProcessor::CalcPageFlipOverlayLayout(const SIZE& renderSize, PageFlipOverlayLayout& layout) const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	layout = {};
	if (!ShouldDrawPageFlipBoxes()) {
		return false;
	}
	if (renderSize.cx <= 0 || renderSize.cy <= 0) {
		return false;
	}

	const int displaySize = std::max(1, m_pageFlipConfig.displaySizeInches);
	const double pixelPitchX = renderSize.cx / (displaySize * 0.87 * 25.4);
	const double pixelPitchY = renderSize.cy / (displaySize * 0.49 * 25.4);
	if (pixelPitchX <= 0.0 || pixelPitchY <= 0.0) {
		return false;
	}

	const int border = m_pageFlipConfig.blackboxBorder;
	const int spacing = m_pageFlipConfig.whiteboxHorizontalSpacing;
	const int whiteSize = m_pageFlipConfig.whiteboxSize;
	const int vertPos = m_pageFlipConfig.whiteboxVerticalPosition;
	const int horizPos = m_pageFlipConfig.whiteboxHorizontalPosition;

	const int blackboxWidthBase = std::max(1, (int)std::lround((whiteSize + spacing + border + border) * pixelPitchX));
	const int blackboxHeightBase = std::max(1, (int)std::lround((vertPos + whiteSize + border) * pixelPitchY));

	int blackboxWidth = blackboxWidthBase;
	int blackboxHeight = blackboxHeightBase;
	const bool calibration = m_pageFlipConfig.calibrationMode;

	const int whiteW = std::max(1, (int)std::lround(whiteSize * pixelPitchX));
	const int whiteH = std::max(1, (int)std::lround(whiteSize * pixelPitchY));
	const int offset1 = (int)std::lround(border * pixelPitchX);
	const int offset2 = (int)std::lround((border + spacing) * pixelPitchX);
	const int vertOffset = (int)std::lround(vertPos * pixelPitchY);
	const double blackboxPosX = (horizPos - border) * pixelPitchX;

	const bool isTop = m_pageFlipConfig.whiteboxCorner == PageFlipCornerPosition::TopLeft
		|| m_pageFlipConfig.whiteboxCorner == PageFlipCornerPosition::TopRight;
	const bool isLeft = m_pageFlipConfig.whiteboxCorner == PageFlipCornerPosition::TopLeft
		|| m_pageFlipConfig.whiteboxCorner == PageFlipCornerPosition::BottomLeft;

	const int baseX = isLeft
		? (int)std::lround(blackboxPosX)
		: renderSize.cx - (int)std::lround(blackboxPosX) - blackboxWidth;
	const int baseY = isTop ? 0 : (renderSize.cy - blackboxHeight);

	const int rightEdge = baseX + blackboxWidth;
	const int whiteTop = isTop
		? baseY + vertOffset
		: baseY + blackboxHeight - whiteH - vertOffset;

	const int whiteLeft1 = isLeft
		? baseX + offset1
		: rightEdge - offset1 - whiteW;
	const int whiteLeft2 = isLeft
		? baseX + offset2
		: rightEdge - offset2 - whiteW;

	layout.black = { baseX, baseY, baseX + blackboxWidth, baseY + blackboxHeight };
	layout.whiteLeft = { whiteLeft1, whiteTop, whiteLeft1 + whiteW, whiteTop + whiteH };
	layout.whiteRight = { whiteLeft2, whiteTop, whiteLeft2 + whiteW, whiteTop + whiteH };
	layout.showBoxes = true;

	if (calibration) {
		const int calibBorder = 40;
		const int extraX = (int)std::lround(calibBorder * pixelPitchX);
		const int extraY = (int)std::lround(calibBorder * pixelPitchY);
		const int extraLeft = extraX / 2;
		const int extraRight = extraX - extraLeft;
		const int extraTop = extraY / 2;
		const int extraBottom = extraY - extraTop;
		layout.calibrationBlack = {
			baseX - extraLeft,
			baseY - extraTop,
			baseX + blackboxWidth + extraRight,
			baseY + blackboxHeight + extraBottom
		};
		layout.showCalibration = true;

		const int centerX = (whiteLeft1 + whiteLeft2 + whiteW) / 2;
		const int centerY = whiteTop + whiteH / 2;
		const int thickness = 1;
		layout.reticleH = { layout.calibrationBlack.left, centerY, layout.calibrationBlack.right, centerY + thickness };
		layout.reticleV = { centerX, layout.calibrationBlack.top, centerX + thickness, layout.calibrationBlack.bottom };
		layout.showReticle = true;
	}

	return true;
}

bool CVideoProcessor::ShouldDrawPageFlipBoxes() const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	if (!m_pageFlipConfig.enabled) {
		return false;
	}

	const int driveModeValue = m_nvidiaVision.IsConnected() ? 2 : (m_pageFlipSerial.IsConnected() ? m_pageFlipConfig.irDriveMode : 0);
	if (driveModeValue != 0) {
		return false;
	}
	return true;
}

bool CVideoProcessor::ShouldDrawPageFlipStatusText() const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	if (!m_pageFlipConfig.enabled) {
		return false;
	}
	return m_pageFlipConfig.showOverlay || IsPageFlipSerialMessageActive();
}

void CVideoProcessor::OnPageFlipSampleReceived()
{
	m_pageFlipHasFrame = true;
	if (m_pageFlipWakeEvent) {
		SetEvent(m_pageFlipWakeEvent);
	}
	EnsurePageFlipThread();
}

void CVideoProcessor::UpdatePageFlipSerialStatus()
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	if (!m_pageFlipConfig.enabled) {
		m_pageFlipSerialWasConnected = false;
		m_pageFlipSerialMessage.clear();
		return;
	}

	const bool connected = m_pageFlipSerial.IsConnected();
	if (connected) {
		m_pageFlipSerialWasConnected = true;
		m_pageFlipSerialMessage.clear();
		return;
	}

	if (m_pageFlipSerialWasConnected && !connected) {
		m_pageFlipSerialWasConnected = false;
		m_pageFlipSerialMessage = L"Serial connection lost";
		m_pageFlipSerialMessageTick = GetPreciseTick();
	}
}

bool CVideoProcessor::IsPageFlipSerialMessageActive() const
{
	std::scoped_lock lock(m_pageFlipStateMutex);
	if (m_pageFlipSerialMessage.empty()) {
		return false;
	}
	const uint64_t now = GetPreciseTick();
	const uint64_t window = GetPreciseTicksPerSecondI() * 2;
	return (now - m_pageFlipSerialMessageTick) < window;
}

bool CVideoProcessor::HandlePageFlipKey(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg != WM_KEYDOWN && uMsg != WM_SYSKEYDOWN) {
		return false;
	}
	std::unique_lock lock(m_pageFlipStateMutex);
	if (!m_pageFlipConfig.calibrationMode) {
		return false;
	}

	UNREFERENCED_PARAMETER(lParam);

	const int key = (int)wParam;
	const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	const int driveModeValue = m_nvidiaVision.IsConnected() ? 2 : (m_pageFlipSerial.IsConnected() ? m_pageFlipConfig.irDriveMode : 0);
	const bool serialMode = driveModeValue == 1;
	const bool nvidiaMode = driveModeValue == 2;

	PageFlipConfig updated = m_pageFlipConfig;
	bool changed = false;
	bool displayChanged = false;
	bool emitterChanged = false;

	auto setMessage = [&](const std::wstring& label, int value) {
		m_pageFlipCalibrationMessage = std::format(L"{}: {}", label, value);
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
	};

	const bool isTop = updated.whiteboxCorner == PageFlipCornerPosition::TopLeft
		|| updated.whiteboxCorner == PageFlipCornerPosition::TopRight;
	const bool isLeft = updated.whiteboxCorner == PageFlipCornerPosition::TopLeft
		|| updated.whiteboxCorner == PageFlipCornerPosition::BottomLeft;

	switch (key) {
	case 'G':
		m_pageFlipShowCalibrationHelp = !m_pageFlipShowCalibrationHelp;
		m_pageFlipCalibrationMessage = m_pageFlipShowCalibrationHelp ? L"calibration help on" : L"calibration help off";
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		return true;
	case 'P': {
		if (serialMode || nvidiaMode) {
			m_pageFlipCalibrationMessage = std::format(L"opt debug logging unavailable in {} mode", nvidiaMode ? L"nvidia_vision" : L"serial");
			m_pageFlipCalibrationMessageTick = GetPreciseTick();
			return true;
		}
		if (!m_pageFlipSerial.IsConnected()) {
			m_pageFlipCalibrationMessage = L"opt debug logging requires emitter connection";
			m_pageFlipCalibrationMessageTick = GetPreciseTick();
			return true;
		}
		const bool enable = !m_pageFlipSerial.IsOptDebugLogging();
		std::wstring logPath;
		const bool ok = m_pageFlipSerial.SetOptDebugLogging(enable, GetPageFlipConfigFolder(m_pageFlipConfigPath), &logPath);
		if (ok) {
			m_pageFlipCalibrationMessage = enable ? L"opt debug logging on" : L"opt debug logging off";
		} else {
			m_pageFlipCalibrationMessage = L"opt debug logging failed";
		}
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		return true;
	}
	case 'C':
		updated.calibrationMode = !updated.calibrationMode;
		m_pageFlipCalibrationMessage = updated.calibrationMode ? L"calibration mode on" : L"calibration mode off";
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		changed = true;
		displayChanged = true;
		break;
	case 'T':
		updated.irDriveMode = (updated.irDriveMode + 1) % 3;
		{
			const wchar_t* modeStr = (updated.irDriveMode == 2) ? L"nvidia_vision" :
				(updated.irDriveMode == 1) ? L"serial" : L"optical";
			m_pageFlipCalibrationMessage = std::format(L"drive mode: {}", modeStr);
		}
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		changed = true;
		displayChanged = true;
		emitterChanged = true;
		break;
	case 'B': {
		if (!m_pageFlipSerial.IsConnected()) {
			m_pageFlipCalibrationMessage = L"save EEPROM requires emitter connection";
		} else if (SavePageFlipEmitterSettings()) {
			m_pageFlipCalibrationMessage = L"emitter settings saved to EEPROM";
		} else {
			m_pageFlipCalibrationMessage = L"save EEPROM failed";
		}
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		return true;
	}
	case 'W':
	case 'S': {
		if (serialMode) {
			return false;
		}
		const int step = shiftDown ? 10 : 1;
		const bool decrease = (key == 'W') ? isTop : !isTop;
		updated.whiteboxVerticalPosition += decrease ? -step : step;
		setMessage(L"whitebox_vertical_position", updated.whiteboxVerticalPosition);
		changed = true;
		displayChanged = true;
		break;
	}
	case 'A':
	case 'D': {
		if (serialMode) {
			return false;
		}
		const int step = shiftDown ? 10 : 1;
		const bool decrease = (key == 'A') ? isLeft : !isLeft;
		updated.whiteboxHorizontalPosition += decrease ? -step : step;
		setMessage(L"whitebox_horizontal_position", updated.whiteboxHorizontalPosition);
		changed = true;
		displayChanged = true;
		break;
	}
	case 'Q':
	case 'E': {
		if (serialMode) {
			return false;
		}
		const int step = shiftDown ? 5 : 1;
		updated.whiteboxHorizontalSpacing = std::max(0, updated.whiteboxHorizontalSpacing + (key == 'Q' ? -step : step));
		setMessage(L"whitebox_horizontal_spacing", updated.whiteboxHorizontalSpacing);
		changed = true;
		displayChanged = true;
		break;
	}
	case 'N':
	case 'M': {
		if (serialMode) {
			return false;
		}
		const int step = shiftDown ? 5 : 1;
		updated.blackboxBorder = std::max(0, updated.blackboxBorder + (key == 'N' ? -step : step));
		setMessage(L"blackbox_border", updated.blackboxBorder);
		changed = true;
		displayChanged = true;
		break;
	}
	case 'Z':
	case 'X': {
		if (serialMode) {
			return false;
		}
		const int step = shiftDown ? 10 : 1;
		updated.whiteboxSize = std::max(1, updated.whiteboxSize + (key == 'Z' ? -step : step));
		setMessage(L"whitebox_size", updated.whiteboxSize);
		changed = true;
		displayChanged = true;
		break;
	}
	case 'I':
	case 'K': {
		const int step = shiftDown ? 200 : 10;
		updated.irFrameDelay = std::max(0, updated.irFrameDelay + (key == 'I' ? -step : step));
		setMessage(L"frame_delay", updated.irFrameDelay);
		changed = true;
		emitterChanged = true;
		break;
	}
	case 'O':
	case 'L': {
		const int step = shiftDown ? 200 : 10;
		updated.irFrameDuration = std::max(0, updated.irFrameDuration + (key == 'O' ? -step : step));
		setMessage(L"frame_duration", updated.irFrameDuration);
		changed = true;
		emitterChanged = true;
		break;
	}
	default:
		return false;
	}

	if (!changed) {
		return false;
	}

	lock.unlock();

	ApplyPageFlipConfig(updated, true);

	if (displayChanged) {
		SavePageFlipConfig(m_pageFlipConfigPath, updated);
	}

	if (emitterChanged && m_pageFlipSerial.IsConnected()) {
		if (m_pageFlipSerial.ApplyEmitterSettings(updated)) {
			SetPageFlipEmitterDirty(true);
		}
	}

	return true;
}

bool CVideoProcessor::HandlePageFlipKeyMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, const wchar_t* source)
{
	if (uMsg != WM_KEYDOWN && uMsg != WM_SYSKEYDOWN) {
		return false;
	}

	const int key = (int)wParam;
	const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool propertyPagesOpen = m_pFilter && m_pFilter->IsPageFlipPropertyPageOpen();
	const PageFlipConfig cfg = GetPageFlipConfig();
	const int calibration = cfg.calibrationMode ? 1 : 0;
	const int enabled = cfg.enabled ? 1 : 0;
	const int driveMode = cfg.irDriveMode;
	m_pageFlipLogger.Log(PageFlipLogLevel::Info,
		L"Pageflip key msg: source={} key={} calibration={} enabled={} drive={}",
		source ? source : L"unknown", key, calibration, enabled, driveMode);

	if (key == VK_F10 && ctrlDown && shiftDown) {
		PageFlipConfig updated = m_pageFlipConfig;
		updated.calibrationMode = !updated.calibrationMode;
		m_pageFlipCalibrationMessage = updated.calibrationMode ? L"calibration mode on" : L"calibration mode off";
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		ApplyPageFlipConfig(updated, true);
		SavePageFlipConfig(m_pageFlipConfigPath, updated);
		m_pageFlipLogger.Log(PageFlipLogLevel::Info,
			L"Pageflip key handled: source={} key={} handled=1",
			source ? source : L"unknown", key);
		return true;
	}
	if (key == VK_F12 && ctrlDown && shiftDown) {
		PageFlipConfig updated = m_pageFlipConfig;
		updated.flipEyes = !updated.flipEyes;
		m_pageFlipCalibrationMessage = updated.flipEyes ? L"flip eyes on" : L"flip eyes off";
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		ApplyPageFlipConfig(updated, true);
		SavePageFlipConfig(m_pageFlipConfigPath, updated);
		m_pageFlipLogger.Log(PageFlipLogLevel::Info,
			L"Pageflip key handled: source={} key={} handled=1",
			source ? source : L"unknown", key);
		return true;
	}
	if (key == VK_F8 && ctrlDown && shiftDown) {
		PageFlipConfig updated = m_pageFlipConfig;
		updated.enabled = !updated.enabled;
		m_pageFlipCalibrationMessage = updated.enabled ? L"pageflip enabled" : L"pageflip disabled";
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		ApplyPageFlipConfig(updated, true);
		SavePageFlipConfig(m_pageFlipConfigPath, updated);
		m_pageFlipLogger.Log(PageFlipLogLevel::Info,
			L"Pageflip key handled: source={} key={} handled=1",
			source ? source : L"unknown", key);
		return true;
	}
	if (key == VK_F9 && ctrlDown && shiftDown) {
		PageFlipConfig updated = m_pageFlipConfig;
		updated.showOverlay = !updated.showOverlay;
		m_pageFlipCalibrationMessage = updated.showOverlay ? L"osd on" : L"osd off";
		m_pageFlipCalibrationMessageTick = GetPreciseTick();
		ApplyPageFlipConfig(updated, true);
		SavePageFlipConfig(m_pageFlipConfigPath, updated);
		m_pageFlipLogger.Log(PageFlipLogLevel::Info,
			L"Pageflip key handled: source={} key={} handled=1",
			source ? source : L"unknown", key);
		return true;
	}
	if (key == VK_F11 && ctrlDown && shiftDown) {
		if (m_pFilter) {
			m_pFilter->ShowPropertyPages();
			m_pageFlipLogger.Log(PageFlipLogLevel::Info,
				L"Pageflip key handled: source={} key={} handled=1",
				source ? source : L"unknown", key);
			return true;
		}
	}

	if (propertyPagesOpen) {
		m_pageFlipLogger.Log(PageFlipLogLevel::Info,
			L"Pageflip key ignored: source={} key={} property_pages_open=1",
			source ? source : L"unknown", key);
		return false;
	}

	const bool handled = HandlePageFlipKey(uMsg, wParam, lParam);
	m_pageFlipLogger.Log(PageFlipLogLevel::Info,
		L"Pageflip key handled: source={} key={} handled={}",
		source ? source : L"unknown", key, handled ? 1 : 0);

	UNREFERENCED_PARAMETER(lParam);
	return handled;
}

void CVideoProcessor::UpdateStatsByWindow()
{
	if (m_iResizeStats == 1) {
		int w = std::max(512, m_windowRect.Width() / 2 - 10) - 5 - 3;
		int h = std::max(280, m_windowRect.Height() - 10) - 5 - 3;
		m_StatsFontH = (int)std::ceil(std::min(w / 36.0, h / 19.4));
		m_StatsFontH &= ~1;
		if (m_StatsFontH < 14) {
			m_StatsFontH = 14;
		}
	}

	CalcStatsParams(); // always run here to recalculate the graph position
}
void CVideoProcessor::UpdateStatsByDisplay()
{
	if (m_iResizeStats == 0) {
		int dpiH = 0;

		if (IsWindows8OrGreater()) {
			static HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
			if (hShcore) {
				typedef HRESULT(WINAPI* tpGetDpiForMonitor)(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);
				static tpGetDpiForMonitor pGetDpiForMonitor = (tpGetDpiForMonitor)GetProcAddress(hShcore, "GetDpiForMonitor");
				if (pGetDpiForMonitor) {
					UINT dpix, dpiy;
					if (S_OK == pGetDpiForMonitor(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpix, &dpiy)) {
						dpiH = dpiy;
					}
				}
			}
		}

		if (!dpiH) {
			HDC hdc = GetDC(nullptr);
			if (hdc) {
				dpiH = GetDeviceCaps(hdc, LOGPIXELSY);
				ReleaseDC(nullptr, hdc);
			}
			else {
				dpiH = 14;
			}
		}

		m_StatsFontH = MulDiv(14, dpiH, 96);

		CalcStatsParams();
	}
}

bool CVideoProcessor::CheckGraphPlacement()
{
	return m_GraphRect.left >= 0 && m_GraphRect.top >= 0
		&& !(m_GraphRect.left < m_StatsRect.right && m_GraphRect.top < m_StatsRect.bottom);
}

void CVideoProcessor::CalcGraphParams()
{
	auto CalcGraphRect = [&]() {
		m_GraphRect.right = m_windowRect.right - 20;
		m_GraphRect.left  = m_GraphRect.right - m_Xstep * m_Syncs.Size();

		m_GraphRect.bottom = m_windowRect.bottom - 20;
		m_GraphRect.top    = m_GraphRect.bottom - 120 * m_Yscale;
	};

	m_Xstep = 4;
	m_Yscale = 2;
	CalcGraphRect();

	if (!CheckGraphPlacement()) {
		m_Xstep = 2;
		m_Yscale = 1;
		CalcGraphRect();
	}

	m_Yaxis = m_GraphRect.bottom - 50 * m_Yscale;
}

void CVideoProcessor::SetDisplayInfo(const DisplayConfig_t& dc, const bool primary, const bool exclusiveScreen)
{
	if (dc.refreshRate.Numerator) {
		m_uHalfRefreshPeriodMs = (UINT32)(500ull * dc.refreshRate.Denominator / dc.refreshRate.Numerator);
	} else {
		m_uHalfRefreshPeriodMs = 0;
	}

	m_strStatsDispInfo.assign(L"\nDisplay: ");

	std::wstring str = DisplayConfigToString(dc);
	if (str.size()) {
		m_strStatsDispInfo.append(str);
		str.clear();

		if (dc.bitsPerChannel) { // if bitsPerChannel is not set then colorEncoding and other values are invalid
			const wchar_t* colenc = ColorEncodingToString(dc.colorEncoding);
			if (colenc) {
				str = std::format(L"\n  Color: {} {}-bit", colenc, dc.bitsPerChannel);
				if (dc.HDRSupported()) {
					str.append(std::format(L" HDR10: {}", dc.HDREnabled() ? L"on" : L"off"));
					if (!dc.HDREnabled() && dc.ACMEnabled()) {
						str.append(L" ACM: on");
					}
				}
			}
		}
	}

	if (primary) {
		m_strStatsDispInfo.append(L" Primary");
	}
	m_strStatsDispInfo.append(exclusiveScreen ? L" exclusive" : L" windowed");

	if (str.size()) {
		m_strStatsDispInfo.append(str);
	}

	UpdatePageFlipRate();
}

void CVideoProcessor::UpdateStatsInputFmt()
{
	m_strStatsInputFmt.assign(L"\nInput format  : ");

	if (m_iSrcFromGPU == 9) {
		m_strStatsInputFmt.append(L"DXVA2_");
	}
	else if (m_iSrcFromGPU == 11) {
		m_strStatsInputFmt.append(L"D3D11_");
	}
	m_strStatsInputFmt.append(m_srcParams.str);

	if (m_srcWidth != m_srcRectWidth || m_srcHeight != m_srcRectHeight) {
		m_strStatsInputFmt += std::format(L" {}x{} ->", m_srcWidth, m_srcHeight);
	}
	m_strStatsInputFmt += std::format(L" {}x{}", m_srcRectWidth, m_srcRectHeight);
	if (m_srcAnamorphic) {
		m_strStatsInputFmt += std::format(L" ({}:{})", m_srcAspectRatioX, m_srcAspectRatioY);
	}

	if (m_srcParams.CSType == CS_YUV) {
		if (m_Dovi.bValid) {
			int dv_profile = 0;
			const auto& hdr = m_Dovi.msd.Header;
			const bool has_el = hdr.el_spatial_resampling_filter_flag && !hdr.disable_residual_flag;

			if ((hdr.vdr_rpu_profile == 0) && hdr.bl_video_full_range_flag) {
				dv_profile = 5;
			} else if (has_el) {
				// Profile 7 is max 12 bits, both MEL & FEL
				if (hdr.vdr_bit_depth == 12) {
					dv_profile = 7;
				} else {
					dv_profile = 4;
				}
			} else {
				dv_profile = 8;
			}

			m_strStatsInputFmt += std::format(L"\n  DolbyVision Profile {}", dv_profile);
		}
		else {
			LPCSTR strs[6] = {};
			GetExtendedFormatString(strs, m_srcExFmt, m_srcParams.CSType);
			m_strStatsInputFmt += std::format(L"\n  Range: {}", A2WStr(strs[1]));
			if (m_decExFmt.NominalRange == DXVA2_NominalRange_Unknown) {
				m_strStatsInputFmt += L'*';
			};
			m_strStatsInputFmt += std::format(L", Matrix: {}", A2WStr(strs[2]));
			if (m_decExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_Unknown) {
				m_strStatsInputFmt += L'*';
			};
			if (m_decExFmt.VideoLighting != DXVA2_VideoLighting_Unknown) {
				// display Lighting only for values other than Unknown, but this never happens
				m_strStatsInputFmt += std::format(L", Lighting: {}", A2WStr(strs[3]));
			};
			m_strStatsInputFmt += std::format(L"\n  Primaries: {}", A2WStr(strs[4]));
			if (m_decExFmt.VideoPrimaries == DXVA2_VideoPrimaries_Unknown) {
				m_strStatsInputFmt += L'*';
			};
			m_strStatsInputFmt += std::format(L", Function: {}", A2WStr(strs[5]));
			if (m_decExFmt.VideoTransferFunction == DXVA2_VideoTransFunc_Unknown) {
				m_strStatsInputFmt += L'*';
			};
			if (m_srcParams.Subsampling == 420) {
				m_strStatsInputFmt += std::format(L"\n  ChromaLocation: {}", A2WStr(strs[0]));
				if (m_decExFmt.VideoChromaSubsampling == DXVA2_VideoChromaSubsampling_Unknown) {
					m_strStatsInputFmt += L'*';
				};
			}
		}
	}
}

void CVideoProcessor::SyncFrameToStreamTime(const REFERENCE_TIME frameStartTime)
{
	if (m_pFilter->m_filterState == State_Running && frameStartTime != INVALID_TIME) {
		if (SUCCEEDED(m_pFilter->StreamTime(m_streamTime)) && frameStartTime > m_streamTime) {
			const auto sleepTime = (frameStartTime - m_streamTime) / 10000LL - m_uHalfRefreshPeriodMs;
			if (sleepTime > 0 && sleepTime < 42) {
				// We are waiting for Preset to display the frame at the required display refresh interval.
				// This is relevant for displays with high frame rates (for example 144 Hz).
				// But no longer than 41 ms to avoid problems with the DVD-Video menu.
				Sleep(static_cast<DWORD>(sleepTime));
			}
		}
	}
}

bool CVideoProcessor::CheckDoviMetadata(const MediaSideDataDOVIMetadata* pDOVIMetadata, const uint8_t maxReshapeMethon)
{
	if (pDOVIMetadata->Header.el_spatial_resampling_filter_flag && !pDOVIMetadata->Header.disable_residual_flag
			&& pDOVIMetadata->Header.vdr_bit_depth != 12) {
		// ignore Profile 4
		return false;
	}

	for (const auto& curve : pDOVIMetadata->Mapping.curves) {
		if (curve.num_pivots < 2 || curve.num_pivots > 9) {
			return false;
		}
		for (int i = 0; i < int(curve.num_pivots - 1); i++) {
			if (curve.mapping_idc[i] > maxReshapeMethon) { // 0 polynomial, 1 mmr
				return false;
			}
		}
	}

	return true;
}

// IUnknown

STDMETHODIMP CVideoProcessor::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv) {
		return E_POINTER;
	}
	if (riid == IID_IUnknown) {
		*ppv = static_cast<IUnknown*>(static_cast<IMFVideoProcessor*>(this));
	}
	else if (riid == IID_IMFVideoProcessor) {
		*ppv = static_cast<IMFVideoProcessor*>(this);
	}
	else if (riid == IID_IMFVideoMixerBitmap) {
		*ppv = static_cast<IMFVideoMixerBitmap*>(this);
	}
	else {
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CVideoProcessor::AddRef()
{
	return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CVideoProcessor::Release()
{
	ULONG uCount = InterlockedDecrement(&m_nRefCount);
	if (uCount == 0) {
		delete this;
	}
	// For thread safety, return a temporary variable.
	return uCount;
}

// IMFVideoProcessor

STDMETHODIMP CVideoProcessor::GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange)
{
	CheckPointer(pPropRange, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	switch (dwProperty) {
	case DXVA2_ProcAmp_Brightness: *pPropRange = m_DXVA2ProcAmpRanges[0]; break;
	case DXVA2_ProcAmp_Contrast:   *pPropRange = m_DXVA2ProcAmpRanges[1]; break;
	case DXVA2_ProcAmp_Hue:        *pPropRange = m_DXVA2ProcAmpRanges[2]; break;
	case DXVA2_ProcAmp_Saturation: *pPropRange = m_DXVA2ProcAmpRanges[3]; break;
	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

STDMETHODIMP CVideoProcessor::GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values)
{
	CheckPointer(Values, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (dwFlags & DXVA2_ProcAmp_Brightness) { Values->Brightness = m_DXVA2ProcAmpValues.Brightness; }
	if (dwFlags & DXVA2_ProcAmp_Contrast)   { Values->Contrast   = m_DXVA2ProcAmpValues.Contrast  ; }
	if (dwFlags & DXVA2_ProcAmp_Hue)        { Values->Hue        = m_DXVA2ProcAmpValues.Hue       ; }
	if (dwFlags & DXVA2_ProcAmp_Saturation) { Values->Saturation = m_DXVA2ProcAmpValues.Saturation; }

	return S_OK;
}

STDMETHODIMP CVideoProcessor::GetBackgroundColor(COLORREF *lpClrBkg)
{
	CheckPointer(lpClrBkg, E_POINTER);
	*lpClrBkg = RGB(0, 0, 0);
	return S_OK;
}

// IMFVideoMixerBitmap

STDMETHODIMP CVideoProcessor::ClearAlphaBitmap()
{
	CAutoLock cRendererLock(&m_pFilter->m_RendererLock);
	m_bAlphaBitmapEnable = false;

	return S_OK;
}

STDMETHODIMP CVideoProcessor::GetAlphaBitmapParameters(MFVideoAlphaBitmapParams *pBmpParms)
{
	CheckPointer(pBmpParms, E_POINTER);
	CAutoLock cRendererLock(&m_pFilter->m_RendererLock);

	if (m_bAlphaBitmapEnable) {
		pBmpParms->dwFlags      = MFVideoAlphaBitmap_SrcRect|MFVideoAlphaBitmap_DestRect;
		pBmpParms->clrSrcKey    = 0; // non used
		pBmpParms->rcSrc        = m_AlphaBitmapRectSrc;
		pBmpParms->nrcDest      = m_AlphaBitmapNRectDest;
		pBmpParms->fAlpha       = 0; // non used
		pBmpParms->dwFilterMode = D3DTEXF_LINEAR;
		return S_OK;
	} else {
		return MF_E_NOT_INITIALIZED;
	}
}
