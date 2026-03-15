#include "stdafx.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
// GUID_DEVINTERFACE_USB_DEVICE from usbiodef.h
// Defined manually to avoid initguid.h conflicts with precompiled headers
// {A5DCBF10-6530-11D2-901F-00C04FB951ED}
static const GUID GUID_DEVINTERFACE_USB_DEVICE_LOCAL =
	{ 0xA5DCBF10, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

#include "PageFlip.h"
#include "Utils/StringUtil.h"
#include "Utils/Util.h"

static const GUID& GetComPortClassGuid()
{
#ifdef GUID_DEVINTERFACE_COMPORT
	return GUID_DEVINTERFACE_COMPORT;
#else
	static const GUID kComPortGuid = { 0x86E0D1E0, 0x8089, 0x11D0, { 0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73 } };
	return kComPortGuid;
#endif
}

static std::wstring GetEnvVar(const wchar_t* name)
{
	const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
	if (!size) {
		return {};
	}
	std::wstring value(size, L'\0');
	if (GetEnvironmentVariableW(name, value.data(), size)) {
		value.resize(wcslen(value.c_str()));
		return value;
	}
	return {};
}

bool operator==(const PageFlipConfig& a, const PageFlipConfig& b)
{
	return a.enabled == b.enabled
		&& a.rateHz == b.rateHz
		&& a.defaultAspect == b.defaultAspect
		&& a.flipEyes == b.flipEyes
		&& a.showOverlay == b.showOverlay
		&& a.logLevel == b.logLevel
		&& a.displayZoomFactor == b.displayZoomFactor
		&& a.displayParallax == b.displayParallax
		&& a.displaySizeInches == b.displaySizeInches
		&& a.whiteboxBrightness == b.whiteboxBrightness
		&& a.whiteboxCorner == b.whiteboxCorner
		&& a.whiteboxVerticalPosition == b.whiteboxVerticalPosition
		&& a.whiteboxHorizontalPosition == b.whiteboxHorizontalPosition
		&& a.whiteboxSize == b.whiteboxSize
		&& a.whiteboxHorizontalSpacing == b.whiteboxHorizontalSpacing
		&& a.blackboxBorder == b.blackboxBorder
		&& a.calibrationMode == b.calibrationMode
		&& a.comPort == b.comPort
		&& a.irDriveMode == b.irDriveMode
		&& a.irProtocol == b.irProtocol
		&& a.irFrameDelay == b.irFrameDelay
		&& a.irFrameDuration == b.irFrameDuration
		&& a.irSignalSpacing == b.irSignalSpacing
		&& a.irFlipEyes == b.irFlipEyes
		&& a.irAverageTimingMode == b.irAverageTimingMode
		&& a.targetFrametime == b.targetFrametime
		&& a.optBlockSignalDetectionDelay == b.optBlockSignalDetectionDelay
		&& a.optIgnoreAllDuplicates == b.optIgnoreAllDuplicates
		&& a.optMinThresholdValueToActivate == b.optMinThresholdValueToActivate
		&& a.optDetectionThresholdHigh == b.optDetectionThresholdHigh
		&& a.optDetectionThresholdLow == b.optDetectionThresholdLow
		&& a.optEnableIgnoreDuringIr == b.optEnableIgnoreDuringIr
		&& a.optEnableDuplicateRealtimeReporting == b.optEnableDuplicateRealtimeReporting
		&& a.optOutputStats == b.optOutputStats
		&& a.optSensorFilterMode == b.optSensorFilterMode;
}

bool operator!=(const PageFlipConfig& a, const PageFlipConfig& b)
{
	return !(a == b);
}

PageFlipConfigBin ToPageFlipConfigBin(const PageFlipConfig& config)
{
	PageFlipConfigBin bin = {};
	bin.enabled = config.enabled ? 1 : 0;
	bin.rateHz = config.rateHz;
	bin.defaultAspect = static_cast<int>(config.defaultAspect);
	bin.flipEyes = config.flipEyes ? 1 : 0;
	bin.showOverlay = config.showOverlay ? 1 : 0;
	bin.logLevel = static_cast<int>(config.logLevel);

	bin.displayZoomFactor = config.displayZoomFactor;
	bin.displayParallax = config.displayParallax;
	bin.displaySizeInches = config.displaySizeInches;
	bin.whiteboxBrightness = config.whiteboxBrightness;
	bin.whiteboxCorner = static_cast<int>(config.whiteboxCorner);
	bin.whiteboxVerticalPosition = config.whiteboxVerticalPosition;
	bin.whiteboxHorizontalPosition = config.whiteboxHorizontalPosition;
	bin.whiteboxSize = config.whiteboxSize;
	bin.whiteboxHorizontalSpacing = config.whiteboxHorizontalSpacing;
	bin.blackboxBorder = config.blackboxBorder;
	bin.calibrationMode = config.calibrationMode ? 1 : 0;

	if (!config.comPort.empty()) {
		wcsncpy_s(bin.comPort, config.comPort.c_str(), _TRUNCATE);
	}
	bin.irDriveMode = config.irDriveMode;
	bin.irProtocol = config.irProtocol;
	bin.irFrameDelay = config.irFrameDelay;
	bin.irFrameDuration = config.irFrameDuration;
	bin.irSignalSpacing = config.irSignalSpacing;
	bin.irFlipEyes = config.irFlipEyes;
	bin.irAverageTimingMode = config.irAverageTimingMode;
	bin.targetFrametime = config.targetFrametime;
	bin.optBlockSignalDetectionDelay = config.optBlockSignalDetectionDelay;
	bin.optIgnoreAllDuplicates = config.optIgnoreAllDuplicates;
	bin.optMinThresholdValueToActivate = config.optMinThresholdValueToActivate;
	bin.optDetectionThresholdHigh = config.optDetectionThresholdHigh;
	bin.optDetectionThresholdLow = config.optDetectionThresholdLow;
	bin.optEnableIgnoreDuringIr = config.optEnableIgnoreDuringIr;
	bin.optEnableDuplicateRealtimeReporting = config.optEnableDuplicateRealtimeReporting;
	bin.optOutputStats = config.optOutputStats;
	bin.optSensorFilterMode = config.optSensorFilterMode;
	return bin;
}

PageFlipConfig FromPageFlipConfigBin(const PageFlipConfigBin& bin, const PageFlipConfig& base)
{
	PageFlipConfig cfg = base;
	cfg.enabled = bin.enabled != 0;
	cfg.rateHz = bin.rateHz;
	cfg.defaultAspect = static_cast<PageFlipAspectMode>(bin.defaultAspect);
	cfg.flipEyes = bin.flipEyes != 0;
	cfg.showOverlay = bin.showOverlay != 0;
	cfg.logLevel = static_cast<PageFlipLogLevel>(bin.logLevel);

	cfg.displayZoomFactor = bin.displayZoomFactor;
	cfg.displayParallax = bin.displayParallax;
	cfg.displaySizeInches = bin.displaySizeInches;
	cfg.whiteboxBrightness = bin.whiteboxBrightness;
	cfg.whiteboxCorner = static_cast<PageFlipCornerPosition>(bin.whiteboxCorner);
	cfg.whiteboxVerticalPosition = bin.whiteboxVerticalPosition;
	cfg.whiteboxHorizontalPosition = bin.whiteboxHorizontalPosition;
	cfg.whiteboxSize = bin.whiteboxSize;
	cfg.whiteboxHorizontalSpacing = bin.whiteboxHorizontalSpacing;
	cfg.blackboxBorder = bin.blackboxBorder;
	cfg.calibrationMode = bin.calibrationMode != 0;

	cfg.comPort = bin.comPort;
	cfg.irDriveMode = bin.irDriveMode;
	cfg.irProtocol = bin.irProtocol;
	cfg.irFrameDelay = bin.irFrameDelay;
	cfg.irFrameDuration = bin.irFrameDuration;
	cfg.irSignalSpacing = bin.irSignalSpacing;
	cfg.irFlipEyes = bin.irFlipEyes;
	cfg.irAverageTimingMode = bin.irAverageTimingMode;
	cfg.targetFrametime = bin.targetFrametime;
	cfg.optBlockSignalDetectionDelay = bin.optBlockSignalDetectionDelay;
	cfg.optIgnoreAllDuplicates = bin.optIgnoreAllDuplicates;
	cfg.optMinThresholdValueToActivate = bin.optMinThresholdValueToActivate;
	cfg.optDetectionThresholdHigh = bin.optDetectionThresholdHigh;
	cfg.optDetectionThresholdLow = bin.optDetectionThresholdLow;
	cfg.optEnableIgnoreDuringIr = bin.optEnableIgnoreDuringIr;
	cfg.optEnableDuplicateRealtimeReporting = bin.optEnableDuplicateRealtimeReporting;
	cfg.optOutputStats = bin.optOutputStats;
	cfg.optSensorFilterMode = bin.optSensorFilterMode;
	return cfg;
}

LocalEmitterSettingsBin ToLocalEmitterSettingsBin(const LocalEmitterSettings& settings)
{
	LocalEmitterSettingsBin bin = {};
	bin.disableAutoConnect = settings.disableAutoConnect ? 1 : 0;
	if (!settings.comPort.empty()) {
		wcsncpy_s(bin.comPort, settings.comPort.c_str(), _TRUNCATE);
	}
	return bin;
}

LocalEmitterSettings FromLocalEmitterSettingsBin(const LocalEmitterSettingsBin& bin)
{
	LocalEmitterSettings settings;
	settings.disableAutoConnect = bin.disableAutoConnect != 0;
	if (bin.comPort[0]) {
		std::wstring port = bin.comPort;
		std::wstring tmp = port;
		str_tolower_all(tmp);
		if (tmp == L"auto") {
			port.clear();
		}
		settings.comPort = port;
	}
	return settings;
}

std::wstring GetDefaultPageFlipConfigPath()
{
	std::wstring base = GetEnvVar(L"APPDATA");
	if (base.empty()) {
		base = GetEnvVar(L"LOCALAPPDATA");
	}
	if (base.empty()) {
		return L"last_display_settings.json";
	}

	std::wstring folder = base + L"\\MPCVR";
	CreateDirectoryW(folder.c_str(), nullptr);

	return folder + L"\\last_display_settings.json";
}

std::wstring GetPageFlipLogPath(const std::wstring& configPath)
{
	const size_t pos = configPath.find_last_of(L"\\/");
	if (pos == std::wstring::npos) {
		return L"Pageflip.log";
	}
	return configPath.substr(0, pos + 1) + L"Pageflip.log";
}

std::wstring GetLocalEmitterSettingsPath(const std::wstring& configPath)
{
	const size_t pos = configPath.find_last_of(L"\\/");
	if (pos == std::wstring::npos) {
		return L"local_emitter_settings.json";
	}
	return configPath.substr(0, pos + 1) + L"local_emitter_settings.json";
}

static bool ParseBoolToken(const std::wstring& value, bool def)
{
	if (value.empty()) {
		return def;
	}

	std::wstring v = value;
	str_tolower_all(v);
	v = str_trim(v);

	if (v == L"1" || v == L"true" || v == L"yes" || v == L"on") {
		return true;
	}
	if (v == L"0" || v == L"false" || v == L"no" || v == L"off") {
		return false;
	}
	return def;
}

static double ParseDoubleToken(const std::wstring& value, double def)
{
	if (value.empty()) {
		return def;
	}
	wchar_t* end = nullptr;
	const double result = std::wcstod(value.c_str(), &end);
	if (end == value.c_str()) {
		return def;
	}
	return result;
}

static int ParseIntToken(const std::wstring& value, int def)
{
	if (value.empty()) {
		return def;
	}
	wchar_t* end = nullptr;
	const long result = std::wcstol(value.c_str(), &end, 10);
	if (end == value.c_str()) {
		return def;
	}
	return static_cast<int>(result);
}

static PageFlipLogLevel ParseLogLevel(const std::wstring& value, PageFlipLogLevel def)
{
	if (value.empty()) {
		return def;
	}

	std::wstring v = value;
	str_tolower_all(v);
	v = str_trim(v);

	if (v == L"error") {
		return PageFlipLogLevel::Error;
	}
	if (v == L"warning" || v == L"warn") {
		return PageFlipLogLevel::Warning;
	}
	if (v == L"info") {
		return PageFlipLogLevel::Info;
	}
	if (v == L"debug") {
		return PageFlipLogLevel::Debug;
	}

	return def;
}

static PageFlipAspectMode ParseAspectMode(const std::wstring& value, PageFlipAspectMode def)
{
	if (value.empty()) {
		return def;
	}

	std::wstring v = value;
	str_tolower_all(v);
	v = str_trim(v);

	if (v == L"side-by-side" || v == L"sbs" || v == L"sidebyside") {
		return PageFlipAspectMode::SideBySide;
	}
	if (v == L"top-and-bottom" || v == L"tab" || v == L"ou" || v == L"over-under") {
		return PageFlipAspectMode::TopAndBottom;
	}

	return def;
}

static PageFlipCornerPosition ParseCornerPosition(const std::wstring& value, PageFlipCornerPosition def)
{
	if (value.empty()) {
		return def;
	}

	std::wstring v = value;
	str_tolower_all(v);
	v = str_trim(v);

	if (v == L"top_left" || v == L"topleft" || v == L"top-left") {
		return PageFlipCornerPosition::TopLeft;
	}
	if (v == L"top_right" || v == L"topright" || v == L"top-right") {
		return PageFlipCornerPosition::TopRight;
	}
	if (v == L"bottom_left" || v == L"bottomleft" || v == L"bottom-left") {
		return PageFlipCornerPosition::BottomLeft;
	}
	if (v == L"bottom_right" || v == L"bottomright" || v == L"bottom-right") {
		return PageFlipCornerPosition::BottomRight;
	}

	return def;
}

static std::wstring CornerPositionToString(PageFlipCornerPosition value)
{
	switch (value) {
	case PageFlipCornerPosition::TopLeft: return L"top_left";
	case PageFlipCornerPosition::TopRight: return L"top_right";
	case PageFlipCornerPosition::BottomLeft: return L"bottom_left";
	case PageFlipCornerPosition::BottomRight: return L"bottom_right";
	default: return L"top_left";
	}
}

static bool FindJsonValue(const std::wstring& json, const std::wstring& key, std::wstring& out)
{
	const std::wstring pattern = L"\"" + key + L"\"";
	size_t pos = json.find(pattern);
	if (pos == std::wstring::npos) {
		return false;
	}
	pos = json.find(L":", pos + pattern.size());
	if (pos == std::wstring::npos) {
		return false;
	}
	++pos;
	while (pos < json.size() && iswspace(json[pos])) {
		++pos;
	}
	if (pos >= json.size()) {
		return false;
	}
	if (json[pos] == L'"') {
		++pos;
		std::wstring value;
		value.reserve(32);
		while (pos < json.size()) {
			const wchar_t c = json[pos++];
			if (c == L'\\' && pos < json.size()) {
				const wchar_t n = json[pos++];
				switch (n) {
				case L'"': value.push_back(L'"'); break;
				case L'\\': value.push_back(L'\\'); break;
				case L'n': value.push_back(L'\n'); break;
				case L'r': value.push_back(L'\r'); break;
				case L't': value.push_back(L'\t'); break;
				default: value.push_back(n); break;
				}
				continue;
			}
			if (c == L'"') {
				break;
			}
			value.push_back(c);
		}
		out = value;
		return true;
	}

	size_t end = pos;
	while (end < json.size() && json[end] != L',' && json[end] != L'}') {
		++end;
	}
	out = str_trim(std::wstring_view(json.data() + pos, end - pos));
	return true;
}

static std::wstring ReadTextFile(const std::wstring& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return {};
	}
	std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (data.empty()) {
		return {};
	}
	return ConvertUtf8orAnsiToWide(data);
}

static bool WriteTextFile(const std::wstring& path, const std::wstring& content)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}
	const std::string data = ConvertWideToUtf8(content);
	file.write(data.data(), data.size());
	return file.good();
}

static void EnsureConfigDirectory(const std::wstring& path)
{
	const size_t pos = path.find_last_of(L"\\/");
	if (pos == std::wstring::npos) {
		return;
	}
	const std::wstring dir = path.substr(0, pos);
	if (!dir.empty()) {
		CreateDirectoryW(dir.c_str(), nullptr);
	}
}

static bool ParseJsonValue(const std::wstring& json, const wchar_t* key, std::wstring& out)
{
	return FindJsonValue(json, key, out);
}

static bool ParseJsonBool(const std::wstring& json, const wchar_t* key, bool def)
{
	std::wstring value;
	if (!ParseJsonValue(json, key, value)) {
		return def;
	}
	return ParseBoolToken(value, def);
}

static double ParseJsonDouble(const std::wstring& json, const wchar_t* key, double def)
{
	std::wstring value;
	if (!ParseJsonValue(json, key, value)) {
		return def;
	}
	return ParseDoubleToken(value, def);
}

static int ParseJsonInt(const std::wstring& json, const wchar_t* key, int def)
{
	std::wstring value;
	if (!ParseJsonValue(json, key, value)) {
		return def;
	}
	return ParseIntToken(value, def);
}

static std::wstring ParseJsonString(const std::wstring& json, const wchar_t* key, const std::wstring& def)
{
	std::wstring value;
	if (!ParseJsonValue(json, key, value)) {
		return def;
	}
	return value;
}

PageFlipConfig LoadPageFlipConfig(const std::wstring& configPath)
{
	PageFlipConfig cfg;
	const std::wstring json = ReadTextFile(configPath);
	if (json.empty()) {
		return cfg;
	}

	cfg.enabled = ParseJsonBool(json, L"pageflip_enabled", cfg.enabled);
	cfg.rateHz = ParseJsonDouble(json, L"target_framerate", cfg.rateHz);
	cfg.defaultAspect = ParseAspectMode(ParseJsonString(json, L"pageflip_default_aspect", L""), cfg.defaultAspect);
	cfg.flipEyes = ParseJsonBool(json, L"pageflip_flip_eyes", cfg.flipEyes);
	cfg.showOverlay = ParseJsonBool(json, L"pageflip_show_overlay", cfg.showOverlay);
	cfg.logLevel = ParseLogLevel(ParseJsonString(json, L"pageflip_log_level", L""), cfg.logLevel);

	cfg.displayZoomFactor = ParseJsonInt(json, L"display_zoom_factor", cfg.displayZoomFactor);
	cfg.displayParallax = ParseJsonDouble(json, L"display_parallax", cfg.displayParallax);
	cfg.displaySizeInches = ParseJsonInt(json, L"display_size", cfg.displaySizeInches);
	cfg.whiteboxBrightness = ParseJsonInt(json, L"whitebox_brightness", cfg.whiteboxBrightness);
	cfg.whiteboxCorner = ParseCornerPosition(ParseJsonString(json, L"whitebox_corner_position", L""), cfg.whiteboxCorner);
	cfg.whiteboxVerticalPosition = ParseJsonInt(json, L"whitebox_vertical_position", cfg.whiteboxVerticalPosition);
	cfg.whiteboxHorizontalPosition = ParseJsonInt(json, L"whitebox_horizontal_position", cfg.whiteboxHorizontalPosition);
	cfg.whiteboxSize = ParseJsonInt(json, L"whitebox_size", cfg.whiteboxSize);
	cfg.whiteboxHorizontalSpacing = ParseJsonInt(json, L"whitebox_horizontal_spacing", cfg.whiteboxHorizontalSpacing);
	cfg.blackboxBorder = ParseJsonInt(json, L"blackbox_border", cfg.blackboxBorder);
	cfg.calibrationMode = ParseJsonBool(json, L"calibration_mode", cfg.calibrationMode);

	cfg.comPort = str_trim(ParseJsonString(json, L"com_port", L""));
	cfg.irDriveMode = ParseJsonInt(json, L"ir_drive_mode", cfg.irDriveMode);

	return cfg;
}

LocalEmitterSettings LoadLocalEmitterSettings(const std::wstring& configPath)
{
	LocalEmitterSettings settings;
	const std::wstring path = GetLocalEmitterSettingsPath(configPath);
	const std::wstring json = ReadTextFile(path);
	if (json.empty()) {
		return settings;
	}

	settings.disableAutoConnect = ParseJsonBool(json, L"disable_auto_connect", settings.disableAutoConnect);
	std::wstring port = str_trim(ParseJsonString(json, L"com_port", L""));
	if (!port.empty()) {
		std::wstring tmp = port;
		str_tolower_all(tmp);
		if (tmp == L"auto") {
			port.clear();
		}
	}
	settings.comPort = port;
	return settings;
}

bool SavePageFlipConfig(const std::wstring& configPath, const PageFlipConfig& config)
{
	if (configPath.empty()) {
		return false;
	}

	EnsureConfigDirectory(configPath);

	std::wstring rateStr = L"0";
	if (config.rateHz > 0.0) {
		rateStr = std::format(L"{:.6g}", config.rateHz);
	}

	const wchar_t* aspect = (config.defaultAspect == PageFlipAspectMode::TopAndBottom)
		? L"top-and-bottom"
		: L"side-by-side";

	const wchar_t* level = L"INFO";
	switch (config.logLevel) {
	case PageFlipLogLevel::Error: level = L"ERROR"; break;
	case PageFlipLogLevel::Warning: level = L"WARNING"; break;
	case PageFlipLogLevel::Info: level = L"INFO"; break;
	case PageFlipLogLevel::Debug: level = L"DEBUG"; break;
	default: level = L"INFO"; break;
	}

	std::wstring parallaxStr = L"0";
	if (config.displayParallax != 0.0) {
		parallaxStr = std::format(L"{:.6g}", config.displayParallax);
	}

	const std::wstring json = std::format(
		L"{{\n"
		L"  \"target_framerate\": \"{}\",\n"
		L"  \"display_zoom_factor\": \"{}\",\n"
		L"  \"display_parallax\": \"{}\",\n"
		L"  \"display_size\": \"{}\",\n"
		L"  \"whitebox_brightness\": \"{}\",\n"
		L"  \"whitebox_corner_position\": \"{}\",\n"
		L"  \"whitebox_vertical_position\": \"{}\",\n"
		L"  \"whitebox_horizontal_position\": \"{}\",\n"
		L"  \"whitebox_size\": \"{}\",\n"
		L"  \"whitebox_horizontal_spacing\": \"{}\",\n"
		L"  \"blackbox_border\": \"{}\",\n"
		L"  \"calibration_mode\": {},\n"
		L"  \"pageflip_enabled\": {},\n"
		L"  \"pageflip_default_aspect\": \"{}\",\n"
		L"  \"pageflip_flip_eyes\": {},\n"
		L"  \"pageflip_show_overlay\": {},\n"
		L"  \"pageflip_log_level\": \"{}\",\n"
		L"  \"com_port\": \"{}\",\n"
		L"  \"ir_drive_mode\": \"{}\"\n"
		L"}}\n",
		rateStr,
		config.displayZoomFactor,
		parallaxStr,
		config.displaySizeInches,
		config.whiteboxBrightness,
		CornerPositionToString(config.whiteboxCorner),
		config.whiteboxVerticalPosition,
		config.whiteboxHorizontalPosition,
		config.whiteboxSize,
		config.whiteboxHorizontalSpacing,
		config.blackboxBorder,
		config.calibrationMode ? L"true" : L"false",
		config.enabled ? L"true" : L"false",
		aspect,
		config.flipEyes ? L"true" : L"false",
		config.showOverlay ? L"true" : L"false",
		level,
		config.comPort,
		config.irDriveMode
	);

	return WriteTextFile(configPath, json);
}

bool SaveLocalEmitterSettings(const std::wstring& configPath, const LocalEmitterSettings& settings)
{
	if (configPath.empty()) {
		return false;
	}

	const std::wstring path = GetLocalEmitterSettingsPath(configPath);
	EnsureConfigDirectory(path);

	std::wstring port = settings.comPort;
	if (port.empty()) {
		port = L"AUTO";
	}

	const std::wstring json = std::format(
		L"{{\n"
		L"  \"disable_auto_connect\": {},\n"
		L"  \"com_port\": \"{}\"\n"
		L"}}\n",
		settings.disableAutoConnect ? L"true" : L"false",
		port
	);

	return WriteTextFile(path, json);
}

bool LoadEmitterSettingsJson(const std::wstring& path, PageFlipConfig& config)
{
	const std::wstring json = ReadTextFile(path);
	if (json.empty()) {
		return false;
	}

	config.irDriveMode = ParseJsonInt(json, L"ir_drive_mode", config.irDriveMode);
	config.irProtocol = ParseJsonInt(json, L"ir_protocol", config.irProtocol);
	config.irFrameDelay = ParseJsonInt(json, L"ir_frame_delay", config.irFrameDelay);
	config.irFrameDuration = ParseJsonInt(json, L"ir_frame_duration", config.irFrameDuration);
	config.irSignalSpacing = ParseJsonInt(json, L"ir_signal_spacing", config.irSignalSpacing);
	config.irFlipEyes = ParseJsonInt(json, L"ir_flip_eyes", config.irFlipEyes);
	config.irAverageTimingMode = ParseJsonInt(json, L"ir_average_timing_mode", config.irAverageTimingMode);
	config.targetFrametime = ParseJsonInt(json, L"target_frametime", config.targetFrametime);
	config.optBlockSignalDetectionDelay = ParseJsonInt(json, L"opt_block_signal_detection_delay", config.optBlockSignalDetectionDelay);
	config.optIgnoreAllDuplicates = ParseJsonInt(json, L"opt_ignore_all_duplicates", config.optIgnoreAllDuplicates);
	config.optSensorFilterMode = ParseJsonInt(json, L"opt_sensor_filter_mode", config.optSensorFilterMode);
	config.optMinThresholdValueToActivate = ParseJsonInt(json, L"opt_min_threshold_value_to_activate", config.optMinThresholdValueToActivate);
	config.optDetectionThresholdHigh = ParseJsonInt(json, L"opt_detection_threshold_high", config.optDetectionThresholdHigh);
	config.optDetectionThresholdLow = ParseJsonInt(json, L"opt_detection_threshold_low", config.optDetectionThresholdLow);
	config.optEnableIgnoreDuringIr = ParseJsonInt(json, L"opt_enable_ignore_during_ir", config.optEnableIgnoreDuringIr);
	config.optEnableDuplicateRealtimeReporting = ParseJsonInt(json, L"opt_enable_duplicate_realtime_reporting", config.optEnableDuplicateRealtimeReporting);
	config.optOutputStats = ParseJsonInt(json, L"opt_output_stats", config.optOutputStats);

	config.optBlockSignalDetectionDelay = ParseJsonInt(json, L"opt101_block_signal_detection_delay", config.optBlockSignalDetectionDelay);
	config.optMinThresholdValueToActivate = ParseJsonInt(json, L"opt101_min_threshold_value_to_activate", config.optMinThresholdValueToActivate);
	config.optDetectionThresholdHigh = ParseJsonInt(json, L"opt101_detection_threshold", config.optDetectionThresholdHigh);
	config.optEnableIgnoreDuringIr = ParseJsonInt(json, L"opt101_enable_ignore_during_ir", config.optEnableIgnoreDuringIr);
	config.optEnableDuplicateRealtimeReporting = ParseJsonInt(json, L"opt101_enable_duplicate_realtime_reporting", config.optEnableDuplicateRealtimeReporting);
	config.optIgnoreAllDuplicates = ParseJsonInt(json, L"opt101_ignore_all_duplicates", config.optIgnoreAllDuplicates);
	config.optSensorFilterMode = ParseJsonInt(json, L"opt101_sensor_filter_mode", config.optSensorFilterMode);

	return true;
}

bool SaveEmitterSettingsJson(const std::wstring& path, const PageFlipConfig& config)
{
	if (path.empty()) {
		return false;
	}

	const std::wstring json = std::format(
		L"{{\n"
		L"  \"ir_drive_mode\": \"{}\",\n"
		L"  \"ir_protocol\": \"{}\",\n"
		L"  \"ir_frame_delay\": \"{}\",\n"
		L"  \"ir_frame_duration\": \"{}\",\n"
		L"  \"ir_signal_spacing\": \"{}\",\n"
		L"  \"ir_flip_eyes\": \"{}\",\n"
		L"  \"ir_average_timing_mode\": \"{}\",\n"
		L"  \"target_frametime\": \"{}\",\n"
		L"  \"opt_block_signal_detection_delay\": \"{}\",\n"
		L"  \"opt_ignore_all_duplicates\": \"{}\",\n"
		L"  \"opt_sensor_filter_mode\": \"{}\",\n"
		L"  \"opt_min_threshold_value_to_activate\": \"{}\",\n"
		L"  \"opt_detection_threshold_high\": \"{}\",\n"
		L"  \"opt_detection_threshold_low\": \"{}\",\n"
		L"  \"opt_enable_ignore_during_ir\": \"{}\",\n"
		L"  \"opt_enable_duplicate_realtime_reporting\": \"{}\",\n"
		L"  \"opt_output_stats\": \"{}\"\n"
		L"}}\n",
		config.irDriveMode,
		config.irProtocol,
		config.irFrameDelay,
		config.irFrameDuration,
		config.irSignalSpacing,
		config.irFlipEyes,
		config.irAverageTimingMode,
		config.targetFrametime,
		config.optBlockSignalDetectionDelay,
		config.optIgnoreAllDuplicates,
		config.optSensorFilterMode,
		config.optMinThresholdValueToActivate,
		config.optDetectionThresholdHigh,
		config.optDetectionThresholdLow,
		config.optEnableIgnoreDuringIr,
		config.optEnableDuplicateRealtimeReporting,
		config.optOutputStats
	);

	return WriteTextFile(path, json);
}

static int ParseComNumber(const std::wstring& port)
{
	if (port.size() < 4) {
		return -1;
	}
	if (_wcsnicmp(port.c_str(), L"COM", 3) != 0) {
		return -1;
	}
	wchar_t* end = nullptr;
	const int num = std::wcstol(port.c_str() + 3, &end, 10);
	if (!end || end == port.c_str() + 3) {
		return -1;
	}
	return num;
}

std::vector<PageFlipPortInfo> EnumeratePageFlipPorts()
{
	std::vector<PageFlipPortInfo> ports;
	const GUID& guid = GetComPortClassGuid();
	HDEVINFO devInfo = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfo == INVALID_HANDLE_VALUE) {
		return ports;
	}

	for (DWORD index = 0; ; ++index) {
		SP_DEVINFO_DATA devData = {};
		devData.cbSize = sizeof(devData);
		if (!SetupDiEnumDeviceInfo(devInfo, index, &devData)) {
			break;
		}

		wchar_t friendly[256] = {};
		wchar_t desc[256] = {};
		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)friendly, sizeof(friendly), nullptr);
		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC, nullptr, (PBYTE)desc, sizeof(desc), nullptr);

		std::wstring info = friendly;
		if (info.empty()) {
			info = desc;
		}

		HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (hKey != INVALID_HANDLE_VALUE) {
			wchar_t portName[64] = {};
			DWORD type = 0;
			DWORD size = sizeof(portName);
			if (RegQueryValueExW(hKey, L"PortName", nullptr, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS) {
				if (portName[0] != L'\0') {
					PageFlipPortInfo item;
					item.port = portName;
					item.description = info;
					bool exists = false;
					for (const auto& existing : ports) {
						if (_wcsicmp(existing.port.c_str(), item.port.c_str()) == 0) {
							exists = true;
							break;
						}
					}
					if (!exists) {
						ports.push_back(std::move(item));
					}
				}
			}
			RegCloseKey(hKey);
		}
	}

	SetupDiDestroyDeviceInfoList(devInfo);

	std::sort(ports.begin(), ports.end(), [](const PageFlipPortInfo& a, const PageFlipPortInfo& b) {
		const int na = ParseComNumber(a.port);
		const int nb = ParseComNumber(b.port);
		if (na >= 0 && nb >= 0 && na != nb) {
			return na < nb;
		}
		return _wcsicmp(a.port.c_str(), b.port.c_str()) < 0;
	});

	return ports;
}

void PageFlipLogger::Configure(const std::wstring& path, PageFlipLogLevel level)
{
	std::scoped_lock lock(m_mutex);
	m_level = level;
	if (m_path != path) {
		if (m_file) {
			fclose(m_file);
			m_file = nullptr;
		}
		m_path = path;
	}
	EnsureFile();
}

void PageFlipLogger::Close()
{
	std::scoped_lock lock(m_mutex);
	if (m_file) {
		fclose(m_file);
		m_file = nullptr;
	}
}

void PageFlipLogger::Log(PageFlipLogLevel level, std::wstring_view msg)
{
	if (level > m_level) {
		return;
	}

	std::scoped_lock lock(m_mutex);
	EnsureFile();
	if (!m_file) {
		return;
	}

	SYSTEMTIME st = {};
	GetLocalTime(&st);
	const std::wstring message(msg);

	fwprintf(
		m_file,
		L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\n",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond,
		st.wMilliseconds,
		LevelToString(level),
		message.c_str()
	);
	fflush(m_file);
}

void PageFlipLogger::EnsureFile()
{
	if (m_path.empty() || m_file) {
		return;
	}
	_wfopen_s(&m_file, m_path.c_str(), L"a, ccs=UTF-8");
}

const wchar_t* PageFlipLogger::LevelToString(PageFlipLogLevel level) const
{
	switch (level) {
	case PageFlipLogLevel::Error: return L"ERROR";
	case PageFlipLogLevel::Warning: return L"WARNING";
	case PageFlipLogLevel::Info: return L"INFO";
	case PageFlipLogLevel::Debug: return L"DEBUG";
	default: return L"UNKNOWN";
	}
}

PageFlipSerial::~PageFlipSerial()
{
	Stop();
}

void PageFlipSerial::Start(const PageFlipConfig& config, PageFlipLogger* logger, bool autoConnectDisabled)
{
	{
		std::scoped_lock lock(m_mutex);
		m_logger = logger;
		m_config = config;
		m_autoConnectDisabled = autoConnectDisabled;
	}
	UpdatePortState();
}

void PageFlipSerial::Stop()
{
	m_userConnected = false;
	{
		std::unique_lock commandLock(m_commandMutex);
		m_commandActive = false;
		m_commandInProgress = false;
		m_commandDone = true;
		m_commandSuccess = false;
		m_commandLines.clear();
		m_commandCv.notify_all();
	}

	if (m_running.load()) {
		m_stopRequested = true;
		if (m_queueEvent) {
			SetEvent(m_queueEvent);
		}
		if (m_thread.joinable()) {
			m_thread.join();
		}
		m_running = false;
		m_stopRequested = false;
	}
	ClosePort();
	if (m_queueEvent) {
		CloseHandle(m_queueEvent);
		m_queueEvent = nullptr;
	}
	m_connected = false;
}

void PageFlipSerial::UpdateConfig(const PageFlipConfig& config)
{
	{
		std::scoped_lock lock(m_mutex);
		m_config = config;
	}
	UpdatePortState();
}

void PageFlipSerial::SetUserConnected(bool connected)
{
	{
		std::scoped_lock lock(m_mutex);
		m_userConnected = connected;
	}
	UpdatePortState();
}

void PageFlipSerial::SetAutoConnectDisabled(bool disabled)
{
	{
		std::scoped_lock lock(m_mutex);
		m_autoConnectDisabled = disabled;
	}
	UpdatePortState();
}

void PageFlipSerial::QueueSignal(PageFlipEye eye)
{
	if (!m_running.load() || !m_connected.load()) {
		return;
	}

	{
		std::scoped_lock lock(m_mutex);
		if (!m_config.enabled || m_config.irDriveMode != 1) {
			return;
		}
	}

	static constexpr char kLeftSignal[] = "9,0\n";
	static constexpr char kRightSignal[] = "9,1\n";

	const char* signal = (eye == PageFlipEye::Left) ? kLeftSignal : kRightSignal;
	const size_t len = strlen(signal);
	if (PushPacket(signal, len) && m_queueEvent) {
		SetEvent(m_queueEvent);
	}
}

bool PageFlipSerial::OpenPort(const std::wstring& portName)
{
	std::wstring path = portName;
	if (path.rfind(L"\\\\.\\", 0) != 0) {
		path = L"\\\\.\\" + path;
	}

	HANDLE handle = CreateFileW(
		path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr
	);

	if (handle == INVALID_HANDLE_VALUE) {
		if (m_logger) {
			m_logger->Log(PageFlipLogLevel::Warning, L"Pageflip serial: failed to open port '{}'.", portName);
		}
		return false;
	}

	COMMTIMEOUTS timeouts = {};
	timeouts.ReadIntervalTimeout = 1;
	timeouts.ReadTotalTimeoutConstant = 2;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 2;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	SetCommTimeouts(handle, &timeouts);

	DCB dcb = {};
	dcb.DCBlength = sizeof(dcb);
	if (GetCommState(handle, &dcb)) {
		dcb.BaudRate = 115200;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;
		SetCommState(handle, &dcb);
	}

	PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

	m_port = handle;

	if (m_logger) {
		m_logger->Log(PageFlipLogLevel::Info, L"Pageflip serial: using port '{}'.", portName);
	}

	return true;
}

std::wstring PageFlipSerial::DetectPort()
{
	std::wstring selected;
	std::wstring fallback;

	const GUID& guid = GetComPortClassGuid();
	HDEVINFO devInfo = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfo == INVALID_HANDLE_VALUE) {
		return {};
	}

	for (DWORD index = 0; ; ++index) {
		SP_DEVINFO_DATA devData = {};
		devData.cbSize = sizeof(devData);
		if (!SetupDiEnumDeviceInfo(devInfo, index, &devData)) {
			break;
		}

		wchar_t friendly[256] = {};
		wchar_t desc[256] = {};

		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)friendly, sizeof(friendly), nullptr);
		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC, nullptr, (PBYTE)desc, sizeof(desc), nullptr);

		std::wstring info = friendly;
		if (info.empty()) {
			info = desc;
		}

		std::wstring infoLower = info;
		str_tolower_all(infoLower);

		HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (hKey != INVALID_HANDLE_VALUE) {
			wchar_t portName[64] = {};
			DWORD type = 0;
			DWORD size = sizeof(portName);
			if (RegQueryValueExW(hKey, L"PortName", nullptr, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS) {
				std::wstring port = portName;
				if (fallback.empty()) {
					fallback = port;
				}
				if (infoLower.find(L"sparkfun pro micro") != std::wstring::npos) {
					selected = port;
				}
			}
			RegCloseKey(hKey);
		}

		if (!selected.empty()) {
			break;
		}
	}

	SetupDiDestroyDeviceInfoList(devInfo);

	if (!selected.empty()) {
		return selected;
	}
	return fallback;
}

std::wstring PageFlipSerial::NormalizePortName(std::wstring name) const
{
	if (name.empty()) {
		return name;
	}
	for (auto& c : name) {
		c = std::towupper(c);
	}
	if (name.rfind(L"COM", 0) != 0) {
		const size_t pos = name.find(L"COM");
		if (pos != std::wstring::npos) {
			name = name.substr(pos);
		}
	}
	return name;
}

void PageFlipSerial::ClosePort()
{
	if (m_port != INVALID_HANDLE_VALUE) {
		CloseHandle(m_port);
		m_port = INVALID_HANDLE_VALUE;
	}
	CloseOptDebugFile();
}

void PageFlipSerial::ThreadProc()
{
	SetThreadName(DWORD(-1), "PageFlipSerial");
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	while (!m_stopRequested.load()) {
		bool doCommand = false;
		std::string command;
		DWORD timeoutMs = 0;
		{
			std::unique_lock lock(m_commandMutex);
			if (m_commandActive && !m_commandInProgress) {
				command = m_commandText;
				timeoutMs = m_commandTimeoutMs;
				m_commandInProgress = true;
				doCommand = true;
			}
		}

		if (doCommand) {
			std::vector<std::string> lines;
			bool ok = WriteLine(command, true);
			const DWORD start = GetTickCount();
			while (ok && !m_stopRequested.load()) {
				std::string line;
				const DWORD elapsed = GetTickCount() - start;
				if (elapsed >= timeoutMs) {
					ok = false;
					break;
				}
				if (!ReadLine(line, timeoutMs - elapsed)) {
					continue;
				}
				if (line == "OK") {
					break;
				}
				if (!line.empty() && line[0] == '+') {
					continue;
				}
				lines.push_back(std::move(line));
			}

			{
				std::unique_lock lock(m_commandMutex);
				m_commandLines = std::move(lines);
				m_commandSuccess = ok;
				m_commandDone = true;
				m_commandInProgress = false;
			}
			m_commandCv.notify_all();
			continue;
		}

		SerialPacket packet;
		while (PopPacket(packet)) {
			WritePacket(packet.data, packet.len);
		}

		if (m_optDebugEnabled.load() && m_port != INVALID_HANDLE_VALUE) {
			DWORD errors = 0;
			COMSTAT stat = {};
			if (ClearCommError(m_port, &errors, &stat) && stat.cbInQue > 0) {
				for (int i = 0; i < 16; ++i) {
					std::string line;
					if (!ReadLine(line, 1)) {
						break;
					}
					if (line.rfind("+o ", 0) == 0) {
						HandleOptDebugLine(line);
					}
				}
			}
		}

		if (m_stopRequested.load()) {
			break;
		}

		if (m_queueEvent) {
			WaitForSingleObject(m_queueEvent, 10);
		} else {
			Sleep(10);
		}

		bool needPort = false;
		{
			std::scoped_lock lock(m_mutex);
			if (m_autoConnectDisabled) {
				needPort = m_userConnected;
			} else {
				needPort = m_userConnected || m_config.enabled;
			}
		}

		if (!m_stopRequested.load() && needPort && m_port == INVALID_HANDLE_VALUE) {
			const DWORD now = GetTickCount();
			if (m_lastConnectAttemptTick == 0 || now - m_lastConnectAttemptTick >= 1000) {
				m_lastConnectAttemptTick = now;
				AttemptConnect();
			}
		}
	}
}

bool PageFlipSerial::PopPacket(SerialPacket& packet)
{
	const uint32_t tail = m_tail.load(std::memory_order_relaxed);
	const uint32_t head = m_head.load(std::memory_order_acquire);
	if (tail == head) {
		return false;
	}
	packet = m_queue[tail];
	const uint32_t next = (tail + 1) % kQueueSize;
	m_tail.store(next, std::memory_order_release);
	return true;
}

bool PageFlipSerial::PushPacket(const char* data, size_t len)
{
	if (!data || !len) {
		return false;
	}

	len = std::min(len, kMaxSignalSize);
	uint32_t head = m_head.load(std::memory_order_relaxed);
	uint32_t next = (head + 1) % kQueueSize;
	uint32_t tail = m_tail.load(std::memory_order_acquire);

	if (next == tail) {
		const uint32_t drop = (tail + 1) % kQueueSize;
		m_tail.store(drop, std::memory_order_release);
	}

	SerialPacket& packet = m_queue[head];
	packet.len = static_cast<uint8_t>(len);
	memset(packet.data, 0, sizeof(packet.data));
	memcpy(packet.data, data, len);

	m_head.store(next, std::memory_order_release);
	return true;
}

bool PageFlipSerial::WritePacket(const char* data, size_t len)
{
	if (m_port == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD written = 0;
	const BOOL ok = WriteFile(m_port, data, (DWORD)len, &written, nullptr);
	if (!ok) {
		HandlePortError();
	}
	return ok == TRUE;
}

bool PageFlipSerial::WriteLine(const std::string& line, bool crlf)
{
	if (line.empty()) {
		return false;
	}
	std::string payload = line;
	if (crlf) {
		payload.append("\r\n");
	} else {
		payload.push_back('\n');
	}
	return WritePacket(payload.data(), payload.size());
}

bool PageFlipSerial::ReadLine(std::string& line, DWORD timeoutMs)
{
	line.clear();
	if (m_port == INVALID_HANDLE_VALUE) {
		return false;
	}
	const DWORD start = GetTickCount();
	for (;;) {
		if (m_stopRequested.load()) {
			return false;
		}
		char ch = 0;
		DWORD read = 0;
		if (!ReadFile(m_port, &ch, 1, &read, nullptr)) {
			HandlePortError();
			return false;
		}
		if (read == 0) {
			if (GetTickCount() - start >= timeoutMs) {
				return false;
			}
			continue;
		}
		if (ch == '\n') {
			return true;
		}
		if (ch == '\r') {
			continue;
		}
		line.push_back(ch);
	}
}

bool PageFlipSerial::SendCommand(const std::string& command, std::vector<std::string>& lines, DWORD timeoutMs)
{
	if (!m_running.load() || m_port == INVALID_HANDLE_VALUE) {
		return false;
	}

	std::unique_lock lock(m_commandMutex);
	if (m_commandActive) {
		return false;
	}
	m_commandActive = true;
	m_commandInProgress = false;
	m_commandDone = false;
	m_commandSuccess = false;
	m_commandTimeoutMs = timeoutMs;
	m_commandText = command;
	m_commandLines.clear();
	lock.unlock();

	if (m_queueEvent) {
		SetEvent(m_queueEvent);
	}

	lock.lock();
	m_commandCv.wait(lock, [&]() { return m_commandDone || m_stopRequested.load(); });
	lines = m_commandLines;
	const bool ok = m_commandSuccess;
	m_commandActive = false;
	m_commandInProgress = false;
	m_commandDone = false;
	m_commandLines.clear();
	return ok;
}

bool PageFlipSerial::ShouldHoldPort() const
{
	if (m_autoConnectDisabled) {
		return m_userConnected;
	}
	return m_userConnected || m_config.enabled;
}

void PageFlipSerial::UpdatePortState()
{
	bool needPort = false;
	{
		std::scoped_lock lock(m_mutex);
		needPort = ShouldHoldPort();
	}
	if (!needPort) {
		if (m_running.load()) {
			m_stopRequested = true;
			if (m_queueEvent) {
				SetEvent(m_queueEvent);
			}
			if (m_thread.joinable()) {
				m_thread.join();
			}
			m_running = false;
			m_stopRequested = false;
		}
		ClosePort();
		m_connected = false;
		return;
	}

	if (!m_queueEvent) {
		m_queueEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	}

	if (!m_running.load()) {
		m_stopRequested = false;
		m_running = true;
		m_thread = std::thread(&PageFlipSerial::ThreadProc, this);
	}

	AttemptConnect();
}

bool PageFlipSerial::AttemptConnect()
{
	if (m_port != INVALID_HANDLE_VALUE) {
		m_connected = true;
		m_disconnectLogged = false;
		return true;
	}

	std::wstring portName;
	{
		std::scoped_lock lock(m_mutex);
		portName = m_config.comPort;
	}

	if (portName.empty()) {
		portName = DetectPort();
	}
	portName = NormalizePortName(portName);

	if (portName.empty()) {
		m_connected = false;
		return false;
	}

	if (portName != m_portName) {
		ClosePort();
		m_portName = portName;
	}

	if (m_port == INVALID_HANDLE_VALUE && !m_portName.empty()) {
		if (!OpenPort(m_portName)) {
			m_connected = false;
			return false;
		}
	}

	if (m_port == INVALID_HANDLE_VALUE) {
		m_connected = false;
		return false;
	}

	m_connected = true;
	m_disconnectLogged = false;
	return true;
}

void PageFlipSerial::HandlePortError()
{
	if (m_port != INVALID_HANDLE_VALUE) {
		ClosePort();
	}
	const bool wasConnected = m_connected.exchange(false);
	if (wasConnected && m_logger && !m_disconnectLogged) {
		m_logger->Log(PageFlipLogLevel::Info, L"Pageflip serial: connection lost.");
		m_disconnectLogged = true;
	}
	m_lastConnectAttemptTick = 0;
}

std::string PageFlipSerial::BuildEmitterSettingsCommand(const PageFlipConfig& config) const
{
	return std::format(
		"0,{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
		config.irProtocol,
		config.irFrameDelay,
		config.irFrameDuration,
		config.irSignalSpacing,
		config.optBlockSignalDetectionDelay,
		config.optMinThresholdValueToActivate,
		config.optDetectionThresholdHigh,
		config.optEnableIgnoreDuringIr,
		config.optEnableDuplicateRealtimeReporting,
		config.optOutputStats,
		config.optIgnoreAllDuplicates,
		config.optSensorFilterMode,
		config.irFlipEyes,
		config.optDetectionThresholdLow,
		config.irAverageTimingMode,
		config.targetFrametime,
		config.irDriveMode
	);
}

bool PageFlipSerial::ReadEmitterSettings(PageFlipConfig& config)
{
	std::vector<std::string> lines;
	if (!SendCommand("0", lines, 5000)) {
		return false;
	}

	std::string payload;
	for (const auto& line : lines) {
		if (line.rfind("parameters", 0) == 0) {
			const size_t pos = line.find(' ');
			if (pos != std::string::npos) {
				payload = line.substr(pos + 1);
			} else {
				payload = line.substr(strlen("parameters"));
			}
			break;
		}
	}
	if (payload.empty()) {
		return false;
	}

	std::vector<std::string> parts;
	str_split(payload, parts, ',');
	if (parts.size() < 18) {
		return false;
	}

	auto parseInt = [](const std::string& value, int def) {
		if (value.empty()) {
			return def;
		}
		char* end = nullptr;
		const long result = std::strtol(value.c_str(), &end, 10);
		if (end == value.c_str()) {
			return def;
		}
		return static_cast<int>(result);
	};

	const int firmware = parseInt(parts[0], 0);
	m_firmwareVersion = firmware;

	if (parts.size() >= 18) {
		size_t idx = 1;
		config.irProtocol = parseInt(parts[idx++], config.irProtocol);
		config.irFrameDelay = parseInt(parts[idx++], config.irFrameDelay);
		config.irFrameDuration = parseInt(parts[idx++], config.irFrameDuration);
		config.irSignalSpacing = parseInt(parts[idx++], config.irSignalSpacing);
		config.optBlockSignalDetectionDelay = parseInt(parts[idx++], config.optBlockSignalDetectionDelay);
		config.optMinThresholdValueToActivate = parseInt(parts[idx++], config.optMinThresholdValueToActivate);
		config.optDetectionThresholdHigh = parseInt(parts[idx++], config.optDetectionThresholdHigh);
		config.optEnableIgnoreDuringIr = parseInt(parts[idx++], config.optEnableIgnoreDuringIr);
		config.optEnableDuplicateRealtimeReporting = parseInt(parts[idx++], config.optEnableDuplicateRealtimeReporting);
		config.optOutputStats = parseInt(parts[idx++], config.optOutputStats);

		config.optIgnoreAllDuplicates = parseInt(parts[idx++], config.optIgnoreAllDuplicates);
		config.optSensorFilterMode = parseInt(parts[idx++], config.optSensorFilterMode);
		config.irFlipEyes = parseInt(parts[idx++], config.irFlipEyes);
		config.optDetectionThresholdLow = parseInt(parts[idx++], config.optDetectionThresholdLow);
		config.irAverageTimingMode = parseInt(parts[idx++], config.irAverageTimingMode);
		config.targetFrametime = parseInt(parts[idx++], config.targetFrametime);
		config.irDriveMode = parseInt(parts[idx++], config.irDriveMode);
	}

	if (m_logger) {
		m_logger->Log(PageFlipLogLevel::Info, L"Pageflip serial: emitter firmware {} detected.", firmware);
	}

	return true;
}

bool PageFlipSerial::ApplyEmitterSettings(const PageFlipConfig& config)
{
	std::vector<std::string> lines;
	const std::string command = BuildEmitterSettingsCommand(config);
	const bool ok = SendCommand(command, lines, 5000);
	if (!ok && m_logger) {
		m_logger->Log(PageFlipLogLevel::Warning, L"Pageflip serial: failed to apply emitter settings.");
	}
	return ok;
}

bool PageFlipSerial::SaveEmitterSettingsToEeprom()
{
	std::vector<std::string> lines;
	const bool ok = SendCommand("8", lines, 5000);
	if (!ok && m_logger) {
		m_logger->Log(PageFlipLogLevel::Warning, L"Pageflip serial: failed to save settings to EEPROM.");
	}
	return ok;
}

bool PageFlipSerial::SetOptDebugLogging(bool enabled, const std::wstring& logDir, std::wstring* outPath)
{
	if (enabled) {
		if (!m_running.load() || m_port == INVALID_HANDLE_VALUE || !m_connected.load()) {
			return false;
		}
		if (m_optDebugEnabled.load()) {
			if (outPath) {
				std::scoped_lock lock(m_optDebugMutex);
				*outPath = m_optDebugLogPath;
			}
			return true;
		}

		std::wstring path;
		if (!OpenOptDebugFile(logDir, path)) {
			return false;
		}

		std::vector<std::string> lines;
		const bool ok = SendCommand("10,1", lines, 2000);
		if (!ok) {
			CloseOptDebugFile();
			return false;
		}
		if (outPath) {
			*outPath = path;
		}
		return true;
	}

	if (m_running.load() && m_port != INVALID_HANDLE_VALUE && m_connected.load()) {
		std::vector<std::string> lines;
		SendCommand("10,0", lines, 2000);
	}
	CloseOptDebugFile();
	if (outPath) {
		outPath->clear();
	}
	return true;
}

bool PageFlipSerial::OpenOptDebugFile(const std::wstring& logDir, std::wstring& path)
{
	std::wstring folder = logDir;
	if (!folder.empty()) {
		const wchar_t last = folder.back();
		if (last != L'\\' && last != L'/') {
			folder.push_back(L'\\');
		}
	}

	SYSTEMTIME st = {};
	GetLocalTime(&st);
	const std::wstring filename = std::format(
		L"{:04}-{:02}-{:02}_{:02}{:02}{:02}_debug_opt_stream_readings.csv",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	path = folder + filename;

	FILE* file = _wfopen(path.c_str(), L"wt");
	if (!file) {
		return false;
	}

	fprintf(file,
		"opt_current_time,left_sensor,right_sensor,duplicate_frames_in_a_row_counter,"
		"opt_block_signal_detection_until,opt_readings_active,opt_sensor_average_timing_mode_resync,opt_sensor_frametime_average_updated,"
		"opt_reading_triggered_left,left_duplicate_detected,left_duplicate_ignored,left_sent_ir,"
		"opt_reading_triggered_right,right_duplicate_detected,right_duplicate_ignored,right_sent_ir\n");

	{
		std::scoped_lock lock(m_optDebugMutex);
		m_optDebugFile = file;
		m_optDebugLogPath = path;
		m_optDebugEnabled.store(true);
	}

	return true;
}

void PageFlipSerial::CloseOptDebugFile()
{
	std::scoped_lock lock(m_optDebugMutex);
	if (m_optDebugFile) {
		fclose(m_optDebugFile);
		m_optDebugFile = nullptr;
	}
	m_optDebugLogPath.clear();
	m_optDebugEnabled.store(false);
}

void PageFlipSerial::HandleOptDebugLine(const std::string& line)
{
	if (line.rfind("+o ", 0) != 0) {
		return;
	}
	const size_t payloadStart = 3;
	if (line.size() <= payloadStart) {
		return;
	}

	const size_t payloadSize = line.size() - payloadStart;
	if (payloadSize < 10) {
		return;
	}

	const unsigned char* raw = reinterpret_cast<const unsigned char*>(line.data() + payloadStart);

	const uint32_t opt_current_time =
		(static_cast<uint32_t>(raw[9] & 0x7F))
		| (static_cast<uint32_t>(raw[8] & 0x7F) << 7)
		| (static_cast<uint32_t>(raw[7] & 0x7F) << 14)
		| (static_cast<uint32_t>(raw[6] & 0x7F) << 21)
		| (static_cast<uint32_t>(raw[5] & 0x0F) << 28);

	const uint32_t left_sensor = ((raw[5] & 0x70) >> 4) | ((raw[4] & 0x1F) << 3);
	const uint32_t right_sensor = ((raw[4] & 0x60) >> 5) | ((raw[3] & 0x3F) << 2);
	const uint32_t duplicate_frames_in_a_row_counter = raw[2] & 0x7F;

	const uint32_t opt_duplicate_frame_left = (raw[0] & 0x20) ? 1 : 0;
	const uint32_t opt_ignore_duplicate_left = (raw[0] & 0x10) ? 1 : 0;
	const uint32_t opt_reading_triggered_left = (raw[0] & 0x08) ? 1 : 0;
	const uint32_t opt_duplicate_frame_right = (raw[0] & 0x04) ? 1 : 0;
	const uint32_t opt_ignore_duplicate_right = (raw[0] & 0x02) ? 1 : 0;
	const uint32_t opt_reading_triggered_right = (raw[0] & 0x01) ? 1 : 0;

	const uint32_t opt_sensor_average_timing_mode_resync = (raw[1] & 0x20) ? 1 : 0;
	const uint32_t opt_sensor_frametime_average_updated = (raw[1] & 0x10) ? 1 : 0;
	const uint32_t opt_readings_active = (raw[1] & 0x08) ? 1 : 0;
	const uint32_t opt_detected_signal_start_eye = (raw[1] & 0x04) ? 1 : 0;
	const uint32_t opt_initiated_sending_ir_signal = (raw[1] & 0x02) ? 1 : 0;
	const uint32_t opt_block_signal_detection_until = (raw[1] & 0x01) ? 1 : 0;

	uint32_t left_sent_ir = 0;
	uint32_t right_sent_ir = 0;
	if (opt_initiated_sending_ir_signal) {
		if (opt_detected_signal_start_eye) {
			left_sent_ir = 1;
		} else {
			right_sent_ir = 1;
		}
	}
	uint32_t left_duplicate_detected = 0;
	uint32_t left_duplicate_ignored = 0;
	if (opt_reading_triggered_left) {
		left_duplicate_detected = opt_duplicate_frame_left;
		if (left_duplicate_detected) {
			left_duplicate_ignored = opt_ignore_duplicate_left;
		}
	}
	uint32_t right_duplicate_detected = 0;
	uint32_t right_duplicate_ignored = 0;
	if (opt_reading_triggered_right) {
		right_duplicate_detected = opt_duplicate_frame_right;
		if (right_duplicate_detected) {
			right_duplicate_ignored = opt_ignore_duplicate_right;
		}
	}

	std::scoped_lock lock(m_optDebugMutex);
	if (!m_optDebugFile) {
		return;
	}

	fprintf(m_optDebugFile,
		"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
		opt_current_time,
		left_sensor,
		right_sensor,
		duplicate_frames_in_a_row_counter,
		opt_block_signal_detection_until,
		opt_readings_active,
		opt_sensor_average_timing_mode_resync,
		opt_sensor_frametime_average_updated,
		opt_reading_triggered_left,
		left_duplicate_detected,
		left_duplicate_ignored,
		left_sent_ir,
		opt_reading_triggered_right,
		right_duplicate_detected,
		right_duplicate_ignored,
		right_sent_ir);
}

// ============================================================================
// NvidiaVisionUSB - NVIDIA 3D Vision IR emitter USB driver
// Based on 3DVisionActivator by FlintEastwood
// ============================================================================

// Known NVIDIA 3D Vision USB hardware IDs (VID 0955)
static const char* const kNvidiaVisionPids[] = {
	"usb#vid_0955&pid_0007",
	"usb#vid_0955&pid_7001",
	"usb#vid_0955&pid_7002",
	"usb#vid_0955&pid_7003",
	"usb#vid_0955&pid_7004",
	"usb#vid_0955&pid_7008",
	"usb#vid_0955&pid_7009",
	"usb#vid_0955&pid_700a",
	"usb#vid_0955&pid_700c",
	"usb#vid_0955&pid_700d&mi_00",
	"usb#vid_0955&pid_700e&mi_00"
};

NvidiaVisionUSB::~NvidiaVisionUSB()
{
	Stop();
}

std::wstring NvidiaVisionUSB::FindUsbDevice()
{
	HDEVINFO devInfo = SetupDiGetClassDevsW(
		&GUID_DEVINTERFACE_USB_DEVICE_LOCAL, nullptr, nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfo == INVALID_HANDLE_VALUE) {
		return {};
	}

	SP_DEVICE_INTERFACE_DATA ifData = {};
	ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr,
		&GUID_DEVINTERFACE_USB_DEVICE_LOCAL, i, &ifData); ++i)
	{
		DWORD requiredSize = 0;
		SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
		if (requiredSize == 0) {
			continue;
		}

		std::vector<BYTE> detailBuf(requiredSize);
		auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuf.data());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, requiredSize, nullptr, nullptr)) {
			continue;
		}

		std::wstring path = detail->DevicePath;
		std::string pathLower(path.begin(), path.end());
		for (auto& c : pathLower) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		for (const char* pid : kNvidiaVisionPids) {
			if (pathLower.find(pid) != std::string::npos) {
				SetupDiDestroyDeviceInfoList(devInfo);
				if (m_logger) {
					m_logger->Log(PageFlipLogLevel::Info, L"NVIDIA Vision: found USB device '{}'.", path);
				}
				return path;
			}
		}
	}

	SetupDiDestroyDeviceInfoList(devInfo);
	return {};
}

HANDLE NvidiaVisionUSB::OpenUsbPipe(const std::wstring& devicePath, const std::wstring& pipeName)
{
	std::wstring fullPath = devicePath + L"\\" + pipeName;
	HANDLE handle = CreateFileW(
		fullPath.c_str(),
		GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE && m_logger) {
		m_logger->Log(PageFlipLogLevel::Warning, L"NVIDIA Vision: failed to open pipe '{}'.", pipeName);
	}
	return handle;
}

template <typename T>
DWORD NvidiaVisionUSB::WriteToPipe(HANDLE pipe, T buffer, int bytes)
{
	if (pipe == INVALID_HANDLE_VALUE) {
		return 0;
	}
	DWORD bytesWritten = 0;
	WriteFile(pipe, reinterpret_cast<const char*>(buffer), bytes, &bytesWritten, nullptr);
	return bytesWritten;
}

bool NvidiaVisionUSB::InitEmitter()
{
	if (m_profiles.empty()) {
		if (m_logger) {
			m_logger->Log(PageFlipLogLevel::Error, L"NVIDIA Vision: no timing profiles loaded.");
		}
		return false;
	}

	const TimingProfile& prof = m_profiles[m_currentProfile];
	float rate = prof.refreshRateHz;
	float x_us = prof.x_us;
	float y_us = prof.y_us;
	float z_us = prof.z_us;
	float w_us = prof.w_us;

	// Convert timing values to emitter clock units
	// T0 runs at 4MHz, T2 runs at 12MHz (48MHz CPU / 12 and /4)
	int x = static_cast<int>(-x_us * 4 + 1);
	int y = static_cast<int>(-y_us * 4 + 1);
	int z = static_cast<int>(-z_us * 12 + 1);
	int w = static_cast<int>(-w_us * 12 + 1);
	int timeout = static_cast<int>(rate * 4); // idle timeout in frames

	int sequence[] = {
		0x00031842,
		0x00180001, w, x, y, 0x22242830, 0x0405080a, z,
		0x00021c01, 0x00000002,
		0x00021e01, timeout,
		0x00011b01, 0x00000007,
		0x00031840
	};

	HANDLE readPipe = OpenUsbPipe(m_devicePath, L"PIPE03");
	char readBuffer[7] = {};

	WriteToPipe(m_pipe0, sequence, 4);            // 42 18 03 00
	if (readPipe != INVALID_HANDLE_VALUE) {
		DWORD bytesRead = 0;
		ReadFile(readPipe, readBuffer, 7, &bytesRead, nullptr);
	}
	WriteToPipe(m_pipe0, sequence + 1, 28);       // timing data
	WriteToPipe(m_pipe0, sequence + 8, 6);        // 01 1c 02 00, 02 00
	WriteToPipe(m_pipe0, sequence + 10, 6);       // 01 1e 02 00, timeout
	WriteToPipe(m_pipe0, sequence + 12, 5);       // 01 1b 01 00, 07
	WriteToPipe(m_pipe0, sequence + 13, 4);       // 40 18 03 00

	if (readPipe != INVALID_HANDLE_VALUE) {
		CloseHandle(readPipe);
	}

	if (m_logger) {
		m_logger->Log(PageFlipLogLevel::Info,
			L"NVIDIA Vision: initialized emitter for '{}' @ {:.3f} Hz (x={:.1f} y={:.1f} z={:.1f} w={:.1f} us).",
			prof.monitorId, rate, x_us, y_us, z_us, w_us);
	}

	return true;
}

void NvidiaVisionUSB::Start(PageFlipLogger* logger)
{
	std::scoped_lock lock(m_mutex);
	m_logger = logger;

	if (m_connected.load()) {
		return;
	}

	m_devicePath = FindUsbDevice();
	if (m_devicePath.empty()) {
		if (m_logger) {
			m_logger->Log(PageFlipLogLevel::Warning, L"NVIDIA Vision: no emitter found.");
		}
		return;
	}

	m_pipe0 = OpenUsbPipe(m_devicePath, L"PIPE02");
	m_pipe1 = OpenUsbPipe(m_devicePath, L"PIPE00");

	if (m_pipe0 == INVALID_HANDLE_VALUE || m_pipe1 == INVALID_HANDLE_VALUE) {
		Stop();
		return;
	}

	if (m_profiles.empty()) {
		TimingProfile defaultProfile;
		defaultProfile.monitorId = L"Default 120Hz";
		defaultProfile.edidId = L"Default";
		m_profiles.push_back(defaultProfile);
	}

	if (!InitEmitter()) {
		Stop();
		return;
	}

	m_connected = true;

	if (m_logger) {
		m_logger->Log(PageFlipLogLevel::Info, L"NVIDIA Vision: emitter started.");
	}
}

void NvidiaVisionUSB::Stop()
{
	std::scoped_lock lock(m_mutex);
	m_connected = false;

	if (m_pipe0 != INVALID_HANDLE_VALUE) {
		CloseHandle(m_pipe0);
		m_pipe0 = INVALID_HANDLE_VALUE;
	}
	if (m_pipe1 != INVALID_HANDLE_VALUE) {
		CloseHandle(m_pipe1);
		m_pipe1 = INVALID_HANDLE_VALUE;
	}
	m_devicePath.clear();

	if (m_logger) {
		m_logger->Log(PageFlipLogLevel::Info, L"NVIDIA Vision: emitter stopped.");
	}
}

void NvidiaVisionUSB::QueueSignal(PageFlipEye eye)
{
	if (!m_connected.load()) {
		return;
	}

	std::scoped_lock lock(m_mutex);
	if (m_pipe1 == INVALID_HANDLE_VALUE) {
		return;
	}

	// Direct synchronous write, matching the original 3DVisionActivator protocol.
	// 0xAA 0xFE 0x00 0x00 = left eye, 0xAA 0xFF 0x00 0x00 = right eye
	// followed by 4 bytes timing offset (0xffff0000 as in original 3DVisionActivator)
	bool isLeft = (eye == PageFlipEye::Left);
	int sequence[] = { isLeft ? 0x0000feaa : 0x0000ffaa, (int)0xffff0000 };
	WriteToPipe(m_pipe1, sequence, 8);
}

void NvidiaVisionUSB::SetTimingProfile(const TimingProfile& profile)
{
	std::scoped_lock lock(m_mutex);
	m_profiles.clear();
	m_profiles.push_back(profile);
	m_currentProfile = 0;
	if (m_connected.load()) {
		InitEmitter();
	}
}

bool NvidiaVisionUSB::LoadTimingProfiles(const std::wstring& path)
{
	std::ifstream fin(path);
	if (!fin.is_open()) {
		if (m_logger) {
			m_logger->Log(PageFlipLogLevel::Warning, L"NVIDIA Vision: cannot open timing file '{}'.", path);
		}
		return false;
	}

	std::vector<TimingProfile> profiles;
	TimingProfile current;
	bool hasData = false;

	std::string line;
	while (std::getline(fin, line)) {
		if (line.empty()) {
			continue;
		}

		auto extractValue = [&](const std::string& key) -> std::string {
			size_t pos = line.find(key);
			if (pos == std::string::npos) {
				return {};
			}
			return line.substr(pos + key.size());
		};

		std::string val;
		if (!(val = extractValue("Monitor:")).empty()) {
			if (hasData) {
				profiles.push_back(current);
				current = TimingProfile{};
			}
			current.monitorId = std::wstring(val.begin(), val.end());
			hasData = true;
		}
		if (!(val = extractValue("EDID_ID:")).empty()) {
			current.edidId = std::wstring(val.begin(), val.end());
		}
		if (!(val = extractValue("RefreshRateHz:")).empty()) {
			current.refreshRateHz = std::stof(val);
		}
		if (!(val = extractValue("X_us:")).empty()) {
			current.x_us = std::stof(val);
		}
		if (!(val = extractValue("Y_us:")).empty()) {
			current.y_us = std::stof(val);
		}
		if (!(val = extractValue("Z_us:")).empty()) {
			current.z_us = std::stof(val);
		}
		if (!(val = extractValue("W_us:")).empty()) {
			current.w_us = std::stof(val);
		}
	}

	if (hasData) {
		profiles.push_back(current);
	}

	if (profiles.empty()) {
		return false;
	}

	std::scoped_lock lock(m_mutex);
	m_profiles = std::move(profiles);
	m_currentProfile = 0;

	if (m_logger) {
		m_logger->Log(PageFlipLogLevel::Info, L"NVIDIA Vision: loaded {} timing profile(s).",
			static_cast<int>(m_profiles.size()));
	}

	if (m_connected.load()) {
		InitEmitter();
	}
	return true;
}

void NvidiaVisionUSB::NextProfile()
{
	std::scoped_lock lock(m_mutex);
	if (m_profiles.empty()) {
		return;
	}
	m_currentProfile = (m_currentProfile + 1) % static_cast<int>(m_profiles.size());
	if (m_connected.load()) {
		InitEmitter();
	}
}
