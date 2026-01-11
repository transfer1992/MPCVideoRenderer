/*
 * (C) 2025 see Authors.txt
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

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

enum class PageFlipLogLevel : int {
	Error = 0,
	Warning = 1,
	Info = 2,
	Debug = 3
};

enum class PageFlipAspectMode : int {
	SideBySide = 0,
	TopAndBottom = 1
};

enum class PageFlipCornerPosition : int {
	TopLeft = 0,
	TopRight = 1,
	BottomLeft = 2,
	BottomRight = 3
};

enum class PageFlipLayout : int {
	None = 0,
	SideBySideFull,
	TopAndBottomFull,
	SideBySideHalf,
	TopAndBottomHalf
};

enum class PageFlipEye : int {
	Left = 0,
	Right = 1
};

struct PageFlipConfig {
	bool enabled = true;
	double rateHz = 0.0;
	PageFlipAspectMode defaultAspect = PageFlipAspectMode::SideBySide;
	bool flipEyes = false;
	bool showOverlay = false;
	PageFlipLogLevel logLevel = PageFlipLogLevel::Info;

	int displayZoomFactor = 100;
	double displayParallax = 0.0;
	int displaySizeInches = 55;
	int whiteboxBrightness = 255;
	PageFlipCornerPosition whiteboxCorner = PageFlipCornerPosition::TopLeft;
	int whiteboxVerticalPosition = 0;
	int whiteboxHorizontalPosition = 0;
	int whiteboxSize = 13;
	int whiteboxHorizontalSpacing = 23;
	int blackboxBorder = 10;
	bool calibrationMode = false;

	std::wstring comPort;
	int irDriveMode = 0;
	int irProtocol = 6;
	int irFrameDelay = 500;
	int irFrameDuration = 7000;
	int irSignalSpacing = 30;
	int irFlipEyes = 0;
	int irAverageTimingMode = 0;
	int targetFrametime = 0;
	int optBlockSignalDetectionDelay = 7500;
	int optIgnoreAllDuplicates = 0;
	int optMinThresholdValueToActivate = 10;
	int optDetectionThresholdHigh = 128;
	int optDetectionThresholdLow = 32;
	int optEnableIgnoreDuringIr = 0;
	int optEnableDuplicateRealtimeReporting = 0;
	int optOutputStats = 0;
	int optSensorFilterMode = 0;
};

struct PageFlipPortInfo {
	std::wstring port;
	std::wstring description;
};

struct PageFlipEmitterState {
	PageFlipConfig config;
	int firmwareVersion = 0;
	bool connected = false;
};

struct LocalEmitterSettings {
	bool disableAutoConnect = false;
	std::wstring comPort;
};

struct PageFlipConfigBin {
	int enabled = 0;
	double rateHz = 0.0;
	int defaultAspect = 0;
	int flipEyes = 0;
	int showOverlay = 0;
	int logLevel = 0;

	int displayZoomFactor = 100;
	double displayParallax = 0.0;
	int displaySizeInches = 55;
	int whiteboxBrightness = 255;
	int whiteboxCorner = 0;
	int whiteboxVerticalPosition = 0;
	int whiteboxHorizontalPosition = 0;
	int whiteboxSize = 13;
	int whiteboxHorizontalSpacing = 23;
	int blackboxBorder = 10;
	int calibrationMode = 0;

	wchar_t comPort[64] = {};
	int irDriveMode = 0;
	int irProtocol = 6;
	int irFrameDelay = 500;
	int irFrameDuration = 7000;
	int irSignalSpacing = 30;
	int irFlipEyes = 0;
	int irAverageTimingMode = 0;
	int targetFrametime = 0;
	int optBlockSignalDetectionDelay = 7500;
	int optIgnoreAllDuplicates = 0;
	int optMinThresholdValueToActivate = 10;
	int optDetectionThresholdHigh = 128;
	int optDetectionThresholdLow = 32;
	int optEnableIgnoreDuringIr = 0;
	int optEnableDuplicateRealtimeReporting = 0;
	int optOutputStats = 0;
	int optSensorFilterMode = 0;
};

struct PageFlipEmitterStateBin {
	PageFlipConfigBin config;
	int firmwareVersion = 0;
	int connected = 0;
};

bool operator==(const PageFlipConfig& a, const PageFlipConfig& b);
bool operator!=(const PageFlipConfig& a, const PageFlipConfig& b);

PageFlipConfigBin ToPageFlipConfigBin(const PageFlipConfig& config);
PageFlipConfig FromPageFlipConfigBin(const PageFlipConfigBin& bin, const PageFlipConfig& base);

std::wstring GetDefaultPageFlipConfigPath();
std::wstring GetPageFlipLogPath(const std::wstring& configPath);
std::wstring GetLocalEmitterSettingsPath(const std::wstring& configPath);
PageFlipConfig LoadPageFlipConfig(const std::wstring& configPath);
bool SavePageFlipConfig(const std::wstring& configPath, const PageFlipConfig& config);
LocalEmitterSettings LoadLocalEmitterSettings(const std::wstring& configPath);
bool SaveLocalEmitterSettings(const std::wstring& configPath, const LocalEmitterSettings& settings);
bool LoadEmitterSettingsJson(const std::wstring& path, PageFlipConfig& config);
bool SaveEmitterSettingsJson(const std::wstring& path, const PageFlipConfig& config);
std::vector<PageFlipPortInfo> EnumeratePageFlipPorts();

struct LocalEmitterSettingsBin {
	int disableAutoConnect = 0;
	wchar_t comPort[64] = {};
};

LocalEmitterSettingsBin ToLocalEmitterSettingsBin(const LocalEmitterSettings& settings);
LocalEmitterSettings FromLocalEmitterSettingsBin(const LocalEmitterSettingsBin& bin);

class PageFlipLogger {
public:
	void Configure(const std::wstring& path, PageFlipLogLevel level);
	void Close();
	PageFlipLogLevel GetLevel() const { return m_level; }

	void Log(PageFlipLogLevel level, std::wstring_view msg);

	template <typename... Args>
	void Log(PageFlipLogLevel level, std::wformat_string<Args...> fmt, Args&&... args)
	{
		if (level > m_level) {
			return;
		}
		Log(level, std::format(fmt, std::forward<Args>(args)...));
	}

private:
	void EnsureFile();
	const wchar_t* LevelToString(PageFlipLogLevel level) const;

	std::mutex m_mutex;
	std::wstring m_path;
	FILE* m_file = nullptr;
	PageFlipLogLevel m_level = PageFlipLogLevel::Error;
};

class PageFlipSerial {
public:
	PageFlipSerial() = default;
	~PageFlipSerial();

	void Start(const PageFlipConfig& config, PageFlipLogger* logger, bool autoConnectDisabled);
	void Stop();
	void UpdateConfig(const PageFlipConfig& config);
	void SetUserConnected(bool connected);
	void SetAutoConnectDisabled(bool disabled);
	void QueueSignal(PageFlipEye eye);

	bool IsRunning() const { return m_running.load(); }
	bool IsConnected() const { return m_connected.load(); }
	int GetFirmwareVersion() const { return m_firmwareVersion.load(); }
	bool IsOptDebugLogging() const { return m_optDebugEnabled.load(); }

	bool ReadEmitterSettings(PageFlipConfig& config);
	bool ApplyEmitterSettings(const PageFlipConfig& config);
	bool SaveEmitterSettingsToEeprom();
	bool SetOptDebugLogging(bool enabled, const std::wstring& logDir, std::wstring* outPath);

private:
	static constexpr size_t kQueueSize = 32;
	static constexpr size_t kMaxSignalSize = 32;

	struct SerialPacket {
		uint8_t len = 0;
		char data[kMaxSignalSize] = {};
	};

	bool OpenPort(const std::wstring& portName);
	std::wstring DetectPort();
	std::wstring NormalizePortName(std::wstring name) const;
	void ClosePort();
	void ThreadProc();
	bool PopPacket(SerialPacket& packet);
	bool PushPacket(const char* data, size_t len);
	bool WritePacket(const char* data, size_t len);
	bool WriteLine(const std::string& line, bool crlf);
	bool ReadLine(std::string& line, DWORD timeoutMs);
	bool SendCommand(const std::string& command, std::vector<std::string>& lines, DWORD timeoutMs);
	bool ShouldHoldPort() const;
	void UpdatePortState();
	bool AttemptConnect();
	void HandlePortError();
	std::string BuildEmitterSettingsCommand(const PageFlipConfig& config) const;
	bool OpenOptDebugFile(const std::wstring& logDir, std::wstring& path);
	void CloseOptDebugFile();
	void HandleOptDebugLine(const std::string& line);

	std::array<SerialPacket, kQueueSize> m_queue = {};
	std::atomic<uint32_t> m_head = 0;
	std::atomic<uint32_t> m_tail = 0;
	HANDLE m_queueEvent = nullptr;

	std::thread m_thread;
	std::atomic<bool> m_running = false;
	std::atomic<bool> m_stopRequested = false;

	std::mutex m_mutex;
	std::wstring m_portName;
	HANDLE m_port = INVALID_HANDLE_VALUE;

	std::atomic<bool> m_connected = false;
	std::atomic<int> m_firmwareVersion = 0;
	PageFlipConfig m_config;
	bool m_userConnected = false;
	bool m_autoConnectDisabled = false;
	DWORD m_lastConnectAttemptTick = 0;
	bool m_disconnectLogged = false;

	std::mutex m_commandMutex;
	std::condition_variable m_commandCv;
	bool m_commandActive = false;
	bool m_commandInProgress = false;
	bool m_commandDone = false;
	bool m_commandSuccess = false;
	DWORD m_commandTimeoutMs = 5000;
	std::string m_commandText;
	std::vector<std::string> m_commandLines;

	PageFlipLogger* m_logger = nullptr;

	std::atomic<bool> m_optDebugEnabled = false;
	std::mutex m_optDebugMutex;
	FILE* m_optDebugFile = nullptr;
	std::wstring m_optDebugLogPath;
};
