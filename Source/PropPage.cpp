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

#include "stdafx.h"
#include "resource.h"
#include "Helper.h"
#include "DisplayConfig.h"
#include "PropPage.h"
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <devguid.h>
#include <functional>
#include <regstr.h>
#include <setupapi.h>
#include <string>
#include <thread>
#include <vector>

void SetCursor(HWND hWnd, LPCWSTR lpCursorName)
{
	SetClassLongPtrW(hWnd, GCLP_HCURSOR, (LONG_PTR)::LoadCursorW(nullptr, lpCursorName));
}

void SetCursor(HWND hWnd, UINT nID, LPCWSTR lpCursorName)
{
	SetCursor(::GetDlgItem(hWnd, nID), lpCursorName);
}

inline void ComboBox_AddStringData(HWND hWnd, int nIDComboBox, LPCWSTR str, LONG_PTR data)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_ADDSTRING, 0, (LPARAM)str);
	if (lValue != CB_ERR) {
		SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETITEMDATA, lValue, data);
	}
}

inline LONG_PTR ComboBox_GetCurItemData(HWND hWnd, int nIDComboBox)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCURSEL, 0, 0);
	if (lValue != CB_ERR) {
		lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, lValue, 0);
	}
	return lValue;
}

void ComboBox_SelectByItemData(HWND hWnd, int nIDComboBox, LONG_PTR data)
{
	LRESULT lCount = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCOUNT, 0, 0);
	if (lCount != CB_ERR) {
		for (int idx = 0; idx < lCount; idx++) {
			const LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, idx, 0);
			if (data == lValue) {
				SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETCURSEL, idx, 0);
				break;
			}
		}
	}
}

static bool BrowseOpenFile(HWND hwnd, const wchar_t* filter, const wchar_t* defExt, std::wstring& outPath);
static std::wstring GetDlgItemTextString(HWND hWnd, int nID);

namespace {

constexpr UINT WM_EMFW_STATUS = WM_APP + 120;
constexpr UINT WM_EMFW_DONE = WM_APP + 121;
static std::wstring g_emfwMatchOverride;

static const GUID& GetComPortClassGuid()
{
#ifdef GUID_DEVINTERFACE_COMPORT
	return GUID_DEVINTERFACE_COMPORT;
#else
	static const GUID kComPortGuid = { 0x86E0D1E0, 0x8089, 0x11D0, { 0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73 } };
	return kComPortGuid;
#endif
}

static bool FileExists(const std::wstring& path)
{
	const DWORD attr = GetFileAttributesW(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring NormalizePortName(const std::wstring& port)
{
	if (port.empty()) {
		return port;
	}
	std::wstring name = port;
	if (name.rfind(L"\\\\.\\", 0) != 0) {
		name = L"\\\\.\\" + name;
	}
	return name;
}

static bool TouchPortAt1200(const std::wstring& port)
{
	const std::wstring path = NormalizePortName(port);
	HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return false;
	}

	DCB dcb = {};
	dcb.DCBlength = sizeof(dcb);
	if (GetCommState(handle, &dcb)) {
		dcb.BaudRate = 1200;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;
		SetCommState(handle, &dcb);
	}

	CloseHandle(handle);
	return true;
}

static std::wstring ToLowerCopy(std::wstring value)
{
	for (auto& ch : value) {
		ch = (wchar_t)std::towlower(ch);
	}
	return value;
}

static bool ContainsNoCase(const std::wstring& text, const std::wstring& needle)
{
	if (text.empty() || needle.empty()) {
		return false;
	}
	const std::wstring hay = ToLowerCopy(text);
	const std::wstring key = ToLowerCopy(needle);
	return hay.find(key) != std::wstring::npos;
}

static double GetPrimaryDisplayRefreshHz()
{
	MONITORINFOEXW mi = {};
	mi.cbSize = sizeof(mi);
	HMONITOR monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	if (!monitor || !GetMonitorInfoW(monitor, &mi)) {
		return 0.0;
	}

	DisplayConfig_t dc = {};
	if (!GetDisplayConfig(mi.szDevice, dc)) {
		return 0.0;
	}
	if (!dc.refreshRate.Numerator || !dc.refreshRate.Denominator) {
		return 0.0;
	}

	return (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
}

static bool MultiSzContainsNoCase(const wchar_t* multi, const std::wstring& needle)
{
	if (!multi || !multi[0] || needle.empty()) {
		return false;
	}
	const wchar_t* cur = multi;
	while (*cur) {
		std::wstring entry = cur;
		if (ContainsNoCase(entry, needle)) {
			return true;
		}
		cur += entry.size() + 1;
	}
	return false;
}

static std::wstring DetectBootloaderPort(const std::wstring& matchOverride)
{
	const GUID& guid = GetComPortClassGuid();
	HDEVINFO devInfo = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfo == INVALID_HANDLE_VALUE) {
		return {};
	}

	std::wstring selectedPreferred;
	std::wstring selectedFallback;
	const bool hasOverride = !matchOverride.empty();

	for (DWORD index = 0; ; ++index) {
		SP_DEVINFO_DATA devData = {};
		devData.cbSize = sizeof(devData);
		if (!SetupDiEnumDeviceInfo(devInfo, index, &devData)) {
			break;
		}

		wchar_t friendly[256] = {};
		wchar_t desc[256] = {};
		wchar_t hwid[512] = {};

		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)friendly, sizeof(friendly), nullptr);
		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC, nullptr, (PBYTE)desc, sizeof(desc), nullptr);
		SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_HARDWAREID, nullptr, (PBYTE)hwid, sizeof(hwid), nullptr);

		std::wstring info = friendly;
		if (info.empty()) {
			info = desc;
		}

		const bool overrideMatch = hasOverride && (ContainsNoCase(info, matchOverride) || MultiSzContainsNoCase(hwid, matchOverride));
		const bool sparkfunBootMatch = ContainsNoCase(info, L"sparkfun pro micro bootloader")
			|| MultiSzContainsNoCase(hwid, L"vid_1b4f&pid_9205")
			|| MultiSzContainsNoCase(hwid, L"1b4f:9205");
		const bool sparkfunRegularMatch = ContainsNoCase(info, L"sparkfun pro micro")
			|| MultiSzContainsNoCase(hwid, L"vid_1b4f&pid_9206")
			|| MultiSzContainsNoCase(hwid, L"1b4f:9206");
		const bool leonardoMatch = ContainsNoCase(info, L"arduino leonardo")
			|| MultiSzContainsNoCase(hwid, L"vid_2341&pid_0036")
			|| MultiSzContainsNoCase(hwid, L"2341:0036");
		const bool usbSerialFallback = !hasOverride && ContainsNoCase(info, L"usb serial device");

		const bool preferredMatch = overrideMatch || sparkfunBootMatch || leonardoMatch;
		const bool fallbackMatch = sparkfunRegularMatch || usbSerialFallback;

		if (!preferredMatch && !fallbackMatch) {
			continue;
		}

		HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (hKey != INVALID_HANDLE_VALUE) {
			wchar_t portName[64] = {};
			DWORD type = 0;
			DWORD size = sizeof(portName);
			if (RegQueryValueExW(hKey, L"PortName", nullptr, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS) {
				if (preferredMatch) {
					selectedPreferred = portName;
					RegCloseKey(hKey);
					break;
				}
				if (selectedFallback.empty()) {
					selectedFallback = portName;
				}
			}
			RegCloseKey(hKey);
		}
	}

	SetupDiDestroyDeviceInfoList(devInfo);
	return !selectedPreferred.empty() ? selectedPreferred : selectedFallback;
}

struct HexImage {
	std::vector<uint8_t> data;
	uint32_t usedStart = 0;
	uint32_t usedEnd = 0;
};

constexpr uint32_t kFlashSize = 0x8000;
constexpr uint32_t kFlashStart = 0x0000;
constexpr uint32_t kFlashEnd = 0x6FFF;
constexpr uint8_t kFlashFill = 0x00;
constexpr uint16_t kPageSize = 128;

static bool ParseHexNibble(wchar_t ch, uint8_t& value)
{
	if (ch >= L'0' && ch <= L'9') {
		value = (uint8_t)(ch - L'0');
		return true;
	}
	if (ch >= L'A' && ch <= L'F') {
		value = (uint8_t)(ch - L'A' + 10);
		return true;
	}
	if (ch >= L'a' && ch <= L'f') {
		value = (uint8_t)(ch - L'a' + 10);
		return true;
	}
	return false;
}

static bool ParseHexByte(const std::wstring& text, size_t pos, uint8_t& value)
{
	if (pos + 1 >= text.size()) {
		return false;
	}
	uint8_t hi = 0;
	uint8_t lo = 0;
	if (!ParseHexNibble(text[pos], hi) || !ParseHexNibble(text[pos + 1], lo)) {
		return false;
	}
	value = (uint8_t)((hi << 4) | lo);
	return true;
}

static bool LoadHexFile(const std::wstring& path, HexImage& image, std::wstring& error)
{
	image.data.assign(kFlashSize, kFlashFill);
	image.usedStart = kFlashStart;
	image.usedEnd = kFlashEnd;

	FILE* file = _wfopen(path.c_str(), L"rt");
	if (!file) {
		error = L"Failed to open firmware file.";
		return false;
	}

	uint32_t upper = 0;
	bool sawData = false;
	bool sawInRange = false;
	wchar_t buffer[1024] = {};
	while (fgetws(buffer, (int)(sizeof(buffer) / sizeof(buffer[0])), file)) {
		std::wstring line = buffer;
		while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}
		if (line.front() != L':') {
			continue;
		}
		if (line.size() < 11) {
			continue;
		}
		uint8_t len = 0;
		uint8_t type = 0;
		uint8_t addrHi = 0;
		uint8_t addrLo = 0;
		if (!ParseHexByte(line, 1, len) || !ParseHexByte(line, 3, addrHi) || !ParseHexByte(line, 5, addrLo)
			|| !ParseHexByte(line, 7, type)) {
			continue;
		}
		const uint16_t addr16 = (uint16_t)((addrHi << 8) | addrLo);

		if (type == 0x00) {
			sawData = true;
			uint32_t base = upper + addr16;
			for (uint8_t i = 0; i < len; ++i) {
				uint8_t data = 0;
				if (!ParseHexByte(line, 9 + i * 2, data)) {
					break;
				}
				const uint32_t addr = base + i;
				if (addr < image.data.size()) {
					image.data[addr] = data;
					if (addr >= kFlashStart && addr <= kFlashEnd) {
						sawInRange = true;
					}
				}
			}
		} else if (type == 0x01) {
			break;
		} else if (type == 0x02) {
			uint8_t hi = 0;
			uint8_t lo = 0;
			if (ParseHexByte(line, 9, hi) && ParseHexByte(line, 11, lo)) {
				upper = ((uint32_t)hi << 12) | ((uint32_t)lo << 4);
			}
		} else if (type == 0x04) {
			uint8_t hi = 0;
			uint8_t lo = 0;
			if (ParseHexByte(line, 9, hi) && ParseHexByte(line, 11, lo)) {
				upper = ((uint32_t)hi << 24) | ((uint32_t)lo << 16);
			}
		}
	}

	fclose(file);

	if (!sawData || !sawInRange) {
		error = L"HEX file contains no data for the firmware range.";
		return false;
	}

	return true;
}

class SerialPort {
public:
	~SerialPort() { Close(); }

	bool Open(const std::wstring& portName, DWORD baud)
	{
		Close();
		std::wstring path = NormalizePortName(portName);
		m_handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (m_handle == INVALID_HANDLE_VALUE) {
			m_handle = nullptr;
			return false;
		}

		DCB dcb = {};
		dcb.DCBlength = sizeof(dcb);
		if (GetCommState(m_handle, &dcb)) {
			dcb.BaudRate = baud;
			dcb.ByteSize = 8;
			dcb.Parity = NOPARITY;
			dcb.StopBits = ONESTOPBIT;
			SetCommState(m_handle, &dcb);
		}

		COMMTIMEOUTS timeouts = {};
		timeouts.ReadIntervalTimeout = 1;
		timeouts.ReadTotalTimeoutConstant = 2;
		timeouts.ReadTotalTimeoutMultiplier = 0;
		timeouts.WriteTotalTimeoutConstant = 20;
		timeouts.WriteTotalTimeoutMultiplier = 0;
		SetCommTimeouts(m_handle, &timeouts);
		PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
		return true;
	}

	void Close()
	{
		if (m_handle) {
			CloseHandle(m_handle);
			m_handle = nullptr;
		}
	}

	bool IsOpen() const { return m_handle != nullptr; }

	bool WriteByte(uint8_t value)
	{
		return WriteData(&value, 1);
	}

	bool WriteData(const uint8_t* data, size_t len)
	{
		if (!m_handle || !data || len == 0) {
			return false;
		}
		DWORD written = 0;
		if (!WriteFile(m_handle, data, (DWORD)len, &written, nullptr)) {
			return false;
		}
		return written == len;
	}

	bool ReadExact(uint8_t* data, size_t len, DWORD timeoutMs)
	{
		if (!m_handle || !data || len == 0) {
			return false;
		}
		const DWORD start = GetTickCount();
		size_t total = 0;
		while (total < len) {
			if (GetTickCount() - start >= timeoutMs) {
				return false;
			}
			DWORD read = 0;
			if (!ReadFile(m_handle, data + total, (DWORD)(len - total), &read, nullptr)) {
				return false;
			}
			if (read == 0) {
				Sleep(1);
				continue;
			}
			total += read;
		}
		return true;
	}

	bool ReadByte(uint8_t& value, DWORD timeoutMs)
	{
		return ReadExact(&value, 1, timeoutMs);
	}

private:
	HANDLE m_handle = nullptr;
};

static bool BootloaderSync(SerialPort& port, std::string& id)
{
	const uint8_t esc = 0x1B;
	for (int i = 0; i < 10; ++i) {
		port.WriteByte(esc);
	}
	const uint8_t cmd = 'S';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t bytes[7] = {};
	if (!port.ReadExact(bytes, sizeof(bytes), 1000)) {
		return false;
	}
	id.assign(reinterpret_cast<char*>(bytes), reinterpret_cast<char*>(bytes) + sizeof(bytes));
	return id == "AVRBOOT" || id == "CATERIN";
}

static bool BootloaderReadSignature(SerialPort& port, uint8_t& sig0, uint8_t& sig1, uint8_t& sig2)
{
	const uint8_t cmd = 's';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t bytes[3] = {};
	if (!port.ReadExact(bytes, sizeof(bytes), 1000)) {
		return false;
	}
	sig2 = bytes[0];
	sig1 = bytes[1];
	sig0 = bytes[2];
	return true;
}

static bool BootloaderSetAddress(SerialPort& port, uint32_t address)
{
	uint8_t cmd = (address < 0x10000) ? 'A' : 'H';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	if (cmd == 'A') {
		uint8_t payload[2] = { (uint8_t)((address >> 8) & 0xFF), (uint8_t)(address & 0xFF) };
		if (!port.WriteData(payload, sizeof(payload))) {
			return false;
		}
	} else {
		uint8_t payload[3] = { (uint8_t)((address >> 16) & 0xFF), (uint8_t)((address >> 8) & 0xFF), (uint8_t)(address & 0xFF) };
		if (!port.WriteData(payload, sizeof(payload))) {
			return false;
		}
	}
	uint8_t ack = 0;
	return port.ReadByte(ack, 1000) && ack == '\r';
}

static bool BootloaderQueryBlockMode(SerialPort& port, uint16_t& blockSize)
{
	const uint8_t cmd = 'b';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t resp = 0;
	if (!port.ReadByte(resp, 1000)) {
		return false;
	}
	if (resp != 'Y') {
		return false;
	}
	uint8_t sizeBytes[2] = {};
	if (!port.ReadExact(sizeBytes, sizeof(sizeBytes), 1000)) {
		return false;
	}
	blockSize = (uint16_t)((sizeBytes[0] << 8) | sizeBytes[1]);
	return true;
}

static bool BootloaderAutoIncrement(SerialPort& port, bool& autoInc)
{
	const uint8_t cmd = 'a';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t resp = 0;
	if (!port.ReadByte(resp, 1000)) {
		return false;
	}
	autoInc = (resp == 'Y');
	return true;
}

static bool BootloaderWriteLow(SerialPort& port, uint8_t value)
{
	const uint8_t cmd = 'c';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	if (!port.WriteByte(value)) {
		return false;
	}
	uint8_t ack = 0;
	return port.ReadByte(ack, 1000) && ack == '\r';
}

static bool BootloaderWriteHigh(SerialPort& port, uint8_t value)
{
	const uint8_t cmd = 'C';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	if (!port.WriteByte(value)) {
		return false;
	}
	uint8_t ack = 0;
	return port.ReadByte(ack, 1000) && ack == '\r';
}

static bool BootloaderWritePage(SerialPort& port)
{
	const uint8_t cmd = 'm';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t ack = 0;
	return port.ReadByte(ack, 1000) && ack == '\r';
}

static bool BootloaderWriteBlock(SerialPort& port, const uint8_t* data, uint16_t count)
{
	uint8_t header[4] = { 'B', (uint8_t)((count >> 8) & 0xFF), (uint8_t)(count & 0xFF), 'F' };
	if (!port.WriteData(header, sizeof(header))) {
		return false;
	}
	if (!port.WriteData(data, count)) {
		return false;
	}
	uint8_t ack = 0;
	return port.ReadByte(ack, 2000) && ack == '\r';
}

static bool BootloaderReadBlock(SerialPort& port, uint8_t* data, uint16_t count)
{
	uint8_t header[4] = { 'g', (uint8_t)((count >> 8) & 0xFF), (uint8_t)(count & 0xFF), 'F' };
	if (!port.WriteData(header, sizeof(header))) {
		return false;
	}
	return port.ReadExact(data, count, 2000);
}

static bool BootloaderReadWord(SerialPort& port, uint8_t& high, uint8_t& low)
{
	const uint8_t cmd = 'R';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t bytes[2] = {};
	if (!port.ReadExact(bytes, sizeof(bytes), 1000)) {
		return false;
	}
	high = bytes[0];
	low = bytes[1];
	return true;
}

static bool BootloaderLeave(SerialPort& port)
{
	const uint8_t cmd = 'E';
	if (!port.WriteByte(cmd)) {
		return false;
	}
	uint8_t ack = 0;
	return port.ReadByte(ack, 1000) && ack == '\r';
}

static bool ProgramFlashBlock(SerialPort& port, const HexImage& image, uint16_t blockSize,
	const std::function<void(int)>& progress, std::wstring& error)
{
	if (blockSize == 0) {
		blockSize = kPageSize;
	}

	const uint32_t total = image.usedEnd - image.usedStart + 1;
	uint32_t nextReport = 5;

	std::vector<uint8_t> buffer;
	for (uint32_t addr = image.usedStart; addr <= image.usedEnd; addr += blockSize) {
		const uint32_t remaining = image.usedEnd - addr + 1;
		uint32_t dataCount = remaining < blockSize ? remaining : blockSize;
		uint32_t count = dataCount;
		if (count & 1) {
			++count;
		}

		buffer.assign(count, 0xFF);
		for (uint32_t i = 0; i < dataCount; ++i) {
			buffer[i] = image.data[addr + i];
		}

		if (!BootloaderSetAddress(port, addr >> 1)) {
			error = L"Failed to set flash address.";
			return false;
		}
		if (!BootloaderWriteBlock(port, buffer.data(), (uint16_t)count)) {
			error = std::format(L"Flash programming failed at 0x{:04X}.", addr);
			return false;
		}

		const uint32_t done = (addr - image.usedStart) + dataCount;
		const int percent = (int)((done * 100) / total);
		if (percent >= (int)nextReport) {
			if (progress) {
				progress(percent);
			}
			nextReport += 5;
		}
	}

	if (progress) {
		progress(100);
	}
	return true;
}

static bool VerifyFlashBlock(SerialPort& port, const HexImage& image, uint16_t blockSize,
	const std::function<void(int)>& progress, std::wstring& error)
{
	if (blockSize == 0) {
		blockSize = kPageSize;
	}

	const uint32_t total = image.usedEnd - image.usedStart + 1;
	uint32_t nextReport = 5;

	std::vector<uint8_t> buffer;
	for (uint32_t addr = image.usedStart; addr <= image.usedEnd; addr += blockSize) {
		const uint32_t remaining = image.usedEnd - addr + 1;
		uint32_t dataCount = remaining < blockSize ? remaining : blockSize;
		uint32_t count = dataCount;
		if (count & 1) {
			++count;
		}

		buffer.assign(count, 0);

		if (!BootloaderSetAddress(port, addr >> 1)) {
			error = L"Failed to set flash address for verify.";
			return false;
		}
		if (!BootloaderReadBlock(port, buffer.data(), (uint16_t)count)) {
			error = std::format(L"Flash read failed at 0x{:04X}.", addr);
			return false;
		}

		for (uint32_t i = 0; i < dataCount; ++i) {
			if (buffer[i] != image.data[addr + i]) {
				error = std::format(L"Verify failed at 0x{:04X}.", addr + i);
				return false;
			}
		}

		const uint32_t done = (addr - image.usedStart) + dataCount;
		const int percent = (int)((done * 100) / total);
		if (percent >= (int)nextReport) {
			if (progress) {
				progress(percent);
			}
			nextReport += 5;
		}
	}

	if (progress) {
		progress(100);
	}
	return true;
}

static bool ProgramFlashByte(SerialPort& port, const HexImage& image,
	const std::function<void(int)>& progress, std::wstring& error)
{
	bool autoInc = false;
	if (!BootloaderAutoIncrement(port, autoInc)) {
		error = L"Failed to query bootloader auto-increment.";
		return false;
	}

	const uint32_t total = image.usedEnd - image.usedStart + 1;
	uint32_t nextReport = 5;

	if (!BootloaderSetAddress(port, image.usedStart >> 1)) {
		error = L"Failed to set initial flash address.";
		return false;
	}

	for (uint32_t addr = image.usedStart; addr <= image.usedEnd; addr += 2) {
		if (!autoInc && !BootloaderSetAddress(port, addr >> 1)) {
			error = L"Failed to set flash address.";
			return false;
		}

		if (!BootloaderWriteLow(port, image.data[addr])) {
			error = std::format(L"Write flash low byte failed at 0x{:04X}.", addr);
			return false;
		}
		if (!BootloaderWriteHigh(port, image.data[addr + 1])) {
			error = std::format(L"Write flash high byte failed at 0x{:04X}.", addr + 1);
			return false;
		}

		if (((addr + 2) % kPageSize) == 0) {
			if (!BootloaderSetAddress(port, addr >> 1)) {
				error = L"Failed to set page address.";
				return false;
			}
			if (!BootloaderWritePage(port)) {
				error = L"Flash page write failed.";
				return false;
			}
		}

		const uint32_t done = (addr - image.usedStart) + 2;
		const int percent = (int)((done * 100) / total);
		if (percent >= (int)nextReport) {
			if (progress) {
				progress(percent);
			}
			nextReport += 5;
		}
	}

	if (progress) {
		progress(100);
	}
	return true;
}

static bool VerifyFlashByte(SerialPort& port, const HexImage& image,
	const std::function<void(int)>& progress, std::wstring& error)
{
	bool autoInc = false;
	if (!BootloaderAutoIncrement(port, autoInc)) {
		error = L"Failed to query bootloader auto-increment.";
		return false;
	}

	const uint32_t total = image.usedEnd - image.usedStart + 1;
	uint32_t nextReport = 5;

	if (!BootloaderSetAddress(port, image.usedStart >> 1)) {
		error = L"Failed to set initial flash address.";
		return false;
	}

	for (uint32_t addr = image.usedStart; addr <= image.usedEnd; addr += 2) {
		if (!autoInc && !BootloaderSetAddress(port, addr >> 1)) {
			error = L"Failed to set flash address.";
			return false;
		}

		uint8_t high = 0;
		uint8_t low = 0;
		if (!BootloaderReadWord(port, high, low)) {
			error = std::format(L"Flash read failed at 0x{:04X}.", addr);
			return false;
		}
		if (low != image.data[addr] || high != image.data[addr + 1]) {
			error = std::format(L"Verify failed at 0x{:04X}.", addr);
			return false;
		}

		const uint32_t done = (addr - image.usedStart) + 2;
		const int percent = (int)((done * 100) / total);
		if (percent >= (int)nextReport) {
			if (progress) {
				progress(percent);
			}
			nextReport += 5;
		}
	}

	if (progress) {
		progress(100);
	}
	return true;
}

class EmitterFirmwareDialog {
public:
	EmitterFirmwareDialog(HWND parent);
	INT_PTR ShowModal();

private:
	static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
	INT_PTR OnDialogMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnInit();
	void OnRefreshPorts();
	void OnSelectFile();
	void OnUpdate();
	void OnClose();

	void AppendStatus(const std::wstring& text);
	void PostStatus(const std::wstring& text);

	std::wstring GetSelectedPort() const;
	void PopulatePorts(const std::wstring& selected);

	void StartWorker();
	void WorkerThread();

	HWND m_parent = nullptr;
	HWND m_hDlg = nullptr;
	std::wstring m_firmwarePath;
	std::wstring m_selectedPort;
	std::wstring m_matchOverride;
	std::atomic<bool> m_running = false;
	std::thread m_worker;
	std::vector<PageFlipPortInfo> m_ports;
	std::wstring m_missingPort;
};

EmitterFirmwareDialog::EmitterFirmwareDialog(HWND parent)
	: m_parent(parent)
{
}

INT_PTR EmitterFirmwareDialog::ShowModal()
{
	return DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_EMITTER_FW_DIALOG), m_parent, DialogProc, (LPARAM)this);
}

INT_PTR CALLBACK EmitterFirmwareDialog::DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG) {
		auto* self = reinterpret_cast<EmitterFirmwareDialog*>(lParam);
		self->m_hDlg = hDlg;
		SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)self);
		self->OnInit();
		return TRUE;
	}

	auto* self = reinterpret_cast<EmitterFirmwareDialog*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
	if (!self) {
		return FALSE;
	}
	return self->OnDialogMessage(msg, wParam, lParam);
}

INT_PTR EmitterFirmwareDialog::OnDialogMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_COMMAND: {
		const int id = LOWORD(wParam);
		const int action = HIWORD(wParam);
		if (action == BN_CLICKED) {
			if (id == IDC_EMFW_REFRESH) {
				OnRefreshPorts();
				return TRUE;
			}
			if (id == IDC_EMFW_SELECT) {
				OnSelectFile();
				return TRUE;
			}
			if (id == IDC_EMFW_UPDATE) {
				OnUpdate();
				return TRUE;
			}
			if (id == IDC_EMFW_CLOSE) {
				OnClose();
				return TRUE;
			}
		}
		break;
	}
	case WM_EMFW_STATUS: {
		auto* text = reinterpret_cast<std::wstring*>(lParam);
		if (text) {
			AppendStatus(*text);
			delete text;
		}
		return TRUE;
	}
	case WM_EMFW_DONE: {
		m_running = false;
		if (m_worker.joinable()) {
			m_worker.join();
		}
		::EnableWindow(::GetDlgItem(m_hDlg, IDC_EMFW_UPDATE), TRUE);
		::EnableWindow(::GetDlgItem(m_hDlg, IDC_EMFW_CLOSE), TRUE);
		return TRUE;
	}
	case WM_CLOSE:
		OnClose();
		return TRUE;
	default:
		break;
	}
	UNREFERENCED_PARAMETER(lParam);
	return FALSE;
}

void EmitterFirmwareDialog::OnInit()
{
	PopulatePorts(L"");
	SetDlgItemTextW(m_hDlg, IDC_EMFW_FILE, L"");
	SetDlgItemTextW(m_hDlg, IDC_EMFW_MATCH, g_emfwMatchOverride.c_str());
	AppendStatus(L"Optional match override accepts a friendly-name or VID/PID substring.");
	AppendStatus(L"Known IDs: regular VID_1B4F&PID_9206, bootloader VID_1B4F&PID_9205.");
}

void EmitterFirmwareDialog::OnRefreshPorts()
{
	const std::wstring selected = GetSelectedPort();
	PopulatePorts(selected);
}

void EmitterFirmwareDialog::OnSelectFile()
{
	std::wstring path;
	if (BrowseOpenFile(m_hDlg, L"HEX Firmware File (*.hex)\0*.hex\0All Files\0*.*\0", L"firmware.hex", path)) {
		m_firmwarePath = path;
		SetDlgItemTextW(m_hDlg, IDC_EMFW_FILE, m_firmwarePath.c_str());
	}
}

void EmitterFirmwareDialog::OnUpdate()
{
	if (m_running.load()) {
		return;
	}
	if (m_firmwarePath.empty()) {
		AppendStatus(L"Select a firmware file first.");
		return;
	}
	if (!FileExists(m_firmwarePath)) {
		AppendStatus(L"Firmware file not found.");
		return;
	}
	m_selectedPort = GetSelectedPort();
	if (m_selectedPort.empty()) {
		AppendStatus(L"Select a serial port first.");
		return;
	}
	m_matchOverride = GetDlgItemTextString(m_hDlg, IDC_EMFW_MATCH);
	g_emfwMatchOverride = m_matchOverride;

	m_running = true;
	::EnableWindow(::GetDlgItem(m_hDlg, IDC_EMFW_UPDATE), FALSE);
	::EnableWindow(::GetDlgItem(m_hDlg, IDC_EMFW_CLOSE), FALSE);
	StartWorker();
}

void EmitterFirmwareDialog::OnClose()
{
	if (m_running.load()) {
		return;
	}
	EndDialog(m_hDlg, 0);
}

void EmitterFirmwareDialog::AppendStatus(const std::wstring& text)
{
	HWND hEdit = GetDlgItem(m_hDlg, IDC_EMFW_STATUS);
	if (!hEdit) {
		return;
	}
	SendMessageW(hEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	std::wstring line = text;
	if (!line.empty() && line.back() != L'\n') {
		line.append(L"\r\n");
	}
	SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

void EmitterFirmwareDialog::PostStatus(const std::wstring& text)
{
	PostMessageW(m_hDlg, WM_EMFW_STATUS, 0, (LPARAM)new std::wstring(text));
}

std::wstring EmitterFirmwareDialog::GetSelectedPort() const
{
	const LONG_PTR data = ComboBox_GetCurItemData(m_hDlg, IDC_EMFW_PORT);
	if (data == -1 || data == CB_ERR) {
		wchar_t buffer[128] = {};
		GetDlgItemTextW(m_hDlg, IDC_EMFW_PORT, buffer, (int)(sizeof(buffer) / sizeof(buffer[0])));
		std::wstring port = buffer;
		port.erase(std::remove_if(port.begin(), port.end(), iswspace), port.end());
		return port;
	}
	if (data == -2) {
		return m_missingPort;
	}
	if (data >= 0 && (size_t)data < m_ports.size()) {
		return m_ports[data].port;
	}
	return {};
}

void EmitterFirmwareDialog::PopulatePorts(const std::wstring& selected)
{
	m_ports = EnumeratePageFlipPorts();
	m_missingPort.clear();

	SendDlgItemMessageW(m_hDlg, IDC_EMFW_PORT, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hDlg, IDC_EMFW_PORT, L"AUTO", -1);

	int selectedIndex = 0;
	std::wstring firstMatch;
	for (size_t i = 0; i < m_ports.size(); ++i) {
		std::wstring label = m_ports[i].port;
		if (!m_ports[i].description.empty()) {
			label.append(L" - ").append(m_ports[i].description);
		}
		ComboBox_AddStringData(m_hDlg, IDC_EMFW_PORT, label.c_str(), (LONG_PTR)i);
		if (!selected.empty() && _wcsicmp(selected.c_str(), m_ports[i].port.c_str()) == 0) {
			selectedIndex = (int)i + 1;
		}
		if (firstMatch.empty() && (ContainsNoCase(m_ports[i].description, L"sparkfun pro micro")
			|| ContainsNoCase(m_ports[i].description, L"sparkfun pro micro usb")
			|| ContainsNoCase(m_ports[i].description, L"vid_1b4f&pid_9206")
			|| ContainsNoCase(m_ports[i].description, L"1b4f:9206")
			|| ContainsNoCase(m_ports[i].description, L"usb serial device"))) {
			firstMatch = m_ports[i].port;
		}
	}

	if (!selected.empty()) {
		// already handled
	} else if (!firstMatch.empty()) {
		for (size_t i = 0; i < m_ports.size(); ++i) {
			if (_wcsicmp(firstMatch.c_str(), m_ports[i].port.c_str()) == 0) {
				selectedIndex = (int)i + 1;
				break;
			}
		}
	}

	if (selected.empty() && selectedIndex == 0) {
		selectedIndex = 0;
	} else if (!selected.empty() && selectedIndex == 0) {
		std::wstring missing = selected;
		missing.append(L" (missing)");
		ComboBox_AddStringData(m_hDlg, IDC_EMFW_PORT, missing.c_str(), -2);
		m_missingPort = selected;
		selectedIndex = (int)SendDlgItemMessageW(m_hDlg, IDC_EMFW_PORT, CB_GETCOUNT, 0, 0) - 1;
	}

	SendDlgItemMessageW(m_hDlg, IDC_EMFW_PORT, CB_SETCURSEL, selectedIndex, 0);
}

void EmitterFirmwareDialog::StartWorker()
{
	m_worker = std::thread(&EmitterFirmwareDialog::WorkerThread, this);
}

void EmitterFirmwareDialog::WorkerThread()
{
	PostStatus(L"Triggering bootloader...");
	if (!TouchPortAt1200(m_selectedPort)) {
		PostStatus(L"Failed to open serial port at 1200 baud.");
		PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
		return;
	}

	std::wstring bootPort;
	for (int i = 0; i < 10; ++i) {
		Sleep(500);
		bootPort = DetectBootloaderPort(m_matchOverride);
		if (!bootPort.empty()) {
			break;
		}
	}

	if (bootPort.empty()) {
		PostStatus(L"Failed to enter bootloader (no matching bootloader device found).");
		PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
		return;
	}

	PostStatus(std::format(L"Bootloader detected on {}.", bootPort));
	Sleep(1000);

	HexImage image;
	std::wstring error;
	if (!LoadHexFile(m_firmwarePath, image, error)) {
		PostStatus(error);
		PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
		return;
	}

	PostStatus(std::format(L"Updating emitter with {}...", m_firmwarePath));

	SerialPort port;
	std::string bootId;
	DWORD usedBaud = 0;
	const DWORD baudRates[] = { 57600, 9600 };
	for (DWORD baud : baudRates) {
		PostStatus(std::format(L"Connecting to bootloader at {} baud...", baud));
		if (!port.Open(bootPort, baud)) {
			PostStatus(std::format(L"Failed to open {} at {} baud.", bootPort, baud));
			continue;
		}
		if (BootloaderSync(port, bootId)) {
			usedBaud = baud;
			break;
		}
		port.Close();
	}

	if (!port.IsOpen()) {
		PostStatus(L"Failed to sync with bootloader.");
		PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
		return;
	}

	PostStatus(std::format(L"Bootloader synced at {} baud ({}).", usedBaud, std::wstring(bootId.begin(), bootId.end())));

	uint8_t sig0 = 0;
	uint8_t sig1 = 0;
	uint8_t sig2 = 0;
	if (BootloaderReadSignature(port, sig0, sig1, sig2)) {
		PostStatus(std::format(L"Signature: {:02X} {:02X} {:02X}.", sig0, sig1, sig2));
		if (!(sig0 == 0x1E && sig1 == 0x95 && sig2 == 0x87)) {
			PostStatus(L"Warning: Unexpected signature (expected 1E 95 87).");
		}
	} else {
		PostStatus(L"Warning: Failed to read signature.");
	}

	uint16_t blockSize = 0;
	bool blockMode = BootloaderQueryBlockMode(port, blockSize);
	if (blockMode && blockSize > 0) {
		PostStatus(std::format(L"Bootloader block mode supported ({} bytes).", blockSize));
	} else {
		blockMode = false;
		PostStatus(L"Bootloader block mode not supported; using byte mode.");
	}

	auto programProgress = [this](int percent) {
		PostStatus(std::format(L"Programming... {}%", percent));
	};
	auto verifyProgress = [this](int percent) {
		PostStatus(std::format(L"Verifying... {}%", percent));
	};

	error.clear();
	bool ok = false;
	if (blockMode) {
		ok = ProgramFlashBlock(port, image, blockSize, programProgress, error);
	} else {
		ok = ProgramFlashByte(port, image, programProgress, error);
	}
	if (!ok) {
		if (error.empty()) {
			error = L"Firmware programming failed.";
		}
		PostStatus(error);
		PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
		return;
	}

	error.clear();
	if (blockMode) {
		ok = VerifyFlashBlock(port, image, blockSize, verifyProgress, error);
	} else {
		ok = VerifyFlashByte(port, image, verifyProgress, error);
	}
	if (!ok) {
		if (error.empty()) {
			error = L"Firmware verify failed.";
		}
		PostStatus(error);
		PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
		return;
	}

	if (!BootloaderLeave(port)) {
		PostStatus(L"Warning: failed to exit bootloader.");
	}

	PostStatus(L"Firmware update completed. Waiting for emitter restart...");
	PostMessageW(m_hDlg, WM_EMFW_DONE, 0, 0);
}

} // namespace

static std::wstring GetDlgItemTextString(HWND hWnd, int nID)
{
	wchar_t buffer[512] = {};
	GetDlgItemTextW(hWnd, nID, buffer, (int)_countof(buffer));
	return buffer;
}


static bool BrowseOpenFile(HWND hwnd, const wchar_t* filter, const wchar_t* defExt, std::wstring& outPath)
{
	wchar_t buffer[MAX_PATH] = {};
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = (DWORD)_countof(buffer);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	ofn.lpstrDefExt = defExt;
	if (!GetOpenFileNameW(&ofn)) {
		return false;
	}
	outPath = buffer;
	return true;
}

static bool BrowseSaveFile(HWND hwnd, const wchar_t* filter, const wchar_t* defExt, std::wstring& outPath)
{
	wchar_t buffer[MAX_PATH] = {};
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = (DWORD)_countof(buffer);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
	ofn.lpstrDefExt = defExt;
	if (!GetSaveFileNameW(&ofn)) {
		return false;
	}
	outPath = buffer;
	return true;
}

// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
	DLog(L"CVRMainPPage()");
}

CVRMainPPage::~CVRMainPPage()
{
	DLog(L"~CVRMainPPage()");
}

void CVRMainPPage::SetControls()
{
	CheckDlgButton(IDC_CHECK1, m_SetsPP.bUseD3D11             ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_SetsPP.bShowStats            ? BST_CHECKED : BST_UNCHECKED);

	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO1, m_SetsPP.iTexFormat);

	CheckDlgButton(IDC_CHECK7, m_SetsPP.VPFmts.bNV12          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK8, m_SetsPP.VPFmts.bP01x          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK9, m_SetsPP.VPFmts.bYUY2          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, m_SetsPP.VPFmts.bOther         ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO9, CB_SETCURSEL, m_SetsPP.iVPDeinterlacing, 0);
	CheckDlgButton(IDC_CHECK3, m_SetsPP.bDeintDouble          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK5, m_SetsPP.bVPScaling            ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO8, CB_SETCURSEL, m_SetsPP.iVPSuperRes, 0);
	CheckDlgButton(IDC_CHECK19, m_SetsPP.bVPRTXVideoHDR       ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(IDC_CHECK18, m_SetsPP.bHdrPreferDoVi       ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK12, m_SetsPP.bHdrPassthrough      ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK14, m_SetsPP.bConvertToSdr        ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO7, CB_SETCURSEL, m_SetsPP.iHdrToggleDisplay, 0);
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETPOS, 1, m_SetsPP.iHdrOsdBrightness);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPOS, 1, m_SetsPP.iSDRDisplayNits / SDR_NITS_STEP);
	GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());

	CheckDlgButton(IDC_CHECK6, m_SetsPP.bInterpolateAt50pct   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK10, m_SetsPP.bUseDither           ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK17, m_SetsPP.bDeintBlend          ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(IDC_CHECK11, m_SetsPP.bExclusiveFS         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK15, m_SetsPP.bVBlankBeforePresent ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK13, m_SetsPP.bAdjustPresentTime   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK16, m_SetsPP.bReinitByDisplay     ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO6, CB_SETCURSEL, m_SetsPP.iResizeStats, 0);

	SendDlgItemMessageW(IDC_COMBO5, CB_SETCURSEL, m_SetsPP.iChromaScaling, 0);
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_SetsPP.iUpscaling, 0);
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_SetsPP.iDownscaling, 0);
	SendDlgItemMessageW(IDC_COMBO4, CB_SETCURSEL, m_SetsPP.iSwapEffect, 0);
}

void CVRMainPPage::EnableControls()
{
	if (!IsWindows8OrGreater()) { // Windows 7
		const BOOL bEnable = !m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_STATIC1).EnableWindow(bEnable); // not working for GROUPBOX
		GetDlgItem(IDC_STATIC2).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK7).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK8).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK9).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK4).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK3).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK5).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC3).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO4).EnableWindow(bEnable);
	}
	else if (IsWindows10OrGreater()) {
		const BOOL bEnable = m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_CHECK12).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC5).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO7).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC6).EnableWindow(bEnable);
		GetDlgItem(IDC_SLIDER1).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC7).EnableWindow(bEnable && m_SetsPP.bVPScaling);
		GetDlgItem(IDC_COMBO8).EnableWindow(bEnable && m_SetsPP.bVPScaling);
#ifdef _WIN64
		GetDlgItem(IDC_CHECK19).EnableWindow(bEnable && m_SetsPP.bHdrPassthrough);
#endif
	}

	GetDlgItem(IDC_STATIC8).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_EDIT1).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_SLIDER2).EnableWindow(m_SetsPP.bConvertToSdr);
}

HRESULT CVRMainPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRMainPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	if (m_SetsPP.iSDRDisplayNits != m_oldSDRDisplayNits) {
		// OK or Apply buttons were not pressed. cancel the settings.
		m_pVideoRenderer->GetSettings(m_SetsPP);
		m_SetsPP.iSDRDisplayNits = m_oldSDRDisplayNits;
		m_pVideoRenderer->SetSettings(m_SetsPP);
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRMainPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	m_pVideoRenderer->GetSettings(m_SetsPP);
	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	if (!IsWindows7SP1OrGreater()) {
		GetDlgItem(IDC_CHECK1).EnableWindow(FALSE);
		m_SetsPP.bUseD3D11 = false;
	}
	if (!IsWindows10OrGreater()) {
		GetDlgItem(IDC_CHECK12).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC5).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO7).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC6).EnableWindow(FALSE);
		GetDlgItem(IDC_SLIDER1).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
		GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
	}

#ifndef _WIN64
	GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
	GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
	GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
#endif

	EnableControls();

	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Fixed font size");
	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Increase font by window");

	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"Auto 8/10-bit Integer",  0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"8-bit Integer",          8);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"10-bit Integer",        10);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"16-bit Floating Point", 16);

	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"Enable");
	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"HACK future frames");

	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for SD");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 720p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1080p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1440p");

	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Do not change");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off");

	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Jinc2m");

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic sharp");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");

	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Discard");
	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Flip");

	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETRANGE, 0, MAKELONG(0, 2));
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETTIC, 0, 1);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETRANGE, 0, MAKELONG(SDR_NITS_MIN / SDR_NITS_STEP, SDR_NITS_MAX / SDR_NITS_STEP));
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETTIC, 0, SDR_NITS_DEF / SDR_NITS_STEP);
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETLINESIZE, 0, 1); // arrow keys
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPAGESIZE, 0, 5); // clicks on trackbar's channel

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	SetControls();

	SetCursor(m_hWnd, IDC_ARROW);
	SetCursor(m_hWnd, IDC_COMBO1, IDC_HAND);

	AddHint(IDC_COMBO8,
		L"Available for Direct3D 11.\n"
		"Requires hardware and driver support:\n"
		"- Intel Graphics UHD 610 or later\n"
		"- Nvidia RTX (x64 only)");
	AddHint(IDC_CHECK19,
		L"Available for Direct3D 11.\n"
		"Requires hardware and driver support:\n"
		"- Nvidia RTX (x64 only)");
	AddHint(IDC_COMBO5,
		L"Used for YUV 4:2:0/4:2:2 input formats\n"
		"when the DVXA2/D3D11 Video Processor is not active.");
	AddHint(IDC_COMBO2,
		L"Used to increase image size when the\n"
		"DVXA2/D3D11 Video Processor is not used for resizing.");
	AddHint(IDC_COMBO3,
		L"Used to reduce image size when the\n"
		"DVXA2/D3D11 Video Processor is not used for resizing.");
	AddHint(IDC_COMBO4,
		L"'Flip' is more efficient, but 'Discard' may work\n"
		"more correctly in some rare situations.");

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		LRESULT lValue;
		const int nID = LOWORD(wParam);
		int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			if (nID == IDC_CHECK1) {
				m_SetsPP.bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK2) {
				m_SetsPP.bShowStats = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK3) {
				m_SetsPP.bDeintDouble = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK5) {
				m_SetsPP.bVPScaling = IsDlgButtonChecked(IDC_CHECK5) == BST_CHECKED;
				SetDirty();
				GetDlgItem(IDC_STATIC7).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				GetDlgItem(IDC_COMBO8).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK6) {
				m_SetsPP.bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK6) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK7) {
				m_SetsPP.VPFmts.bNV12 = IsDlgButtonChecked(IDC_CHECK7) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK8) {
				m_SetsPP.VPFmts.bP01x = IsDlgButtonChecked(IDC_CHECK8) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK9) {
				m_SetsPP.VPFmts.bYUY2 = IsDlgButtonChecked(IDC_CHECK9) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK4) {
				m_SetsPP.VPFmts.bOther = IsDlgButtonChecked(IDC_CHECK4) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK10) {
				m_SetsPP.bUseDither = IsDlgButtonChecked(IDC_CHECK10) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK17) {
				m_SetsPP.bDeintBlend = IsDlgButtonChecked(IDC_CHECK17) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK11) {
				m_SetsPP.bExclusiveFS = IsDlgButtonChecked(IDC_CHECK11) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK15) {
				m_SetsPP.bVBlankBeforePresent = IsDlgButtonChecked(IDC_CHECK15) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK13) {
				m_SetsPP.bAdjustPresentTime = IsDlgButtonChecked(IDC_CHECK13) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK16) {
				m_SetsPP.bReinitByDisplay = IsDlgButtonChecked(IDC_CHECK16) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK18) {
				m_SetsPP.bHdrPreferDoVi = IsDlgButtonChecked(IDC_CHECK18) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK12) {
				m_SetsPP.bHdrPassthrough = IsDlgButtonChecked(IDC_CHECK12) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK14) {
				m_SetsPP.bConvertToSdr = IsDlgButtonChecked(IDC_CHECK14) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK19) {
				m_SetsPP.bVPRTXVideoHDR = IsDlgButtonChecked(IDC_CHECK19) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_BUTTON1) {
				m_SetsPP.SetDefault();
				SetControls();
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (action == CBN_SELCHANGE) {
			if (nID == IDC_COMBO6) {
				lValue = SendDlgItemMessageW(IDC_COMBO6, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iResizeStats) {
					m_SetsPP.iResizeStats = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO1) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO1);
				if (lValue != m_SetsPP.iTexFormat) {
					m_SetsPP.iTexFormat = lValue;
					SetDirty();

					GetDlgItem(IDC_CHECK19).EnableWindow(m_SetsPP.bUseD3D11 && m_SetsPP.bHdrPassthrough && m_SetsPP.iTexFormat != TEXFMT_8INT);
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO9) {
				lValue = SendDlgItemMessageW(IDC_COMBO9, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPDeinterlacing) {
					m_SetsPP.iVPDeinterlacing = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO8) {
				lValue = SendDlgItemMessageW(IDC_COMBO8, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPSuperRes) {
					m_SetsPP.iVPSuperRes = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO7) {
				lValue = SendDlgItemMessageW(IDC_COMBO7, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iHdrToggleDisplay) {
					m_SetsPP.iHdrToggleDisplay = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO5) {
				lValue = SendDlgItemMessageW(IDC_COMBO5, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iChromaScaling) {
					m_SetsPP.iChromaScaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO2) {
				lValue = SendDlgItemMessageW(IDC_COMBO2, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iUpscaling) {
					m_SetsPP.iUpscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO3) {
				lValue = SendDlgItemMessageW(IDC_COMBO3, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iDownscaling) {
					m_SetsPP.iDownscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO4) {
				lValue = SendDlgItemMessageW(IDC_COMBO4, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iSwapEffect) {
					m_SetsPP.iSwapEffect = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
		}
	}
	else if (uMsg == WM_HSCROLL) {
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER1)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER1, TBM_GETPOS, 0, 0);
			if (lValue != m_SetsPP.iHdrOsdBrightness) {
				m_SetsPP.iHdrOsdBrightness = lValue;
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER2)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER2, TBM_GETPOS, 0, 0);
			lValue *= SDR_NITS_STEP;
			if (lValue != m_SetsPP.iSDRDisplayNits) {
				m_SetsPP.iSDRDisplayNits = lValue;
				GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());
				SetDirty();
				{
					// apply only SDRDisplayNits
					Settings_t sets;
					m_pVideoRenderer->GetSettings(sets);
					sets.iSDRDisplayNits = m_SetsPP.iSDRDisplayNits;
					m_pVideoRenderer->SetSettings(sets);
				}
			}
			return (LRESULT)1;
		}
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	m_pVideoRenderer->SetSettings(m_SetsPP);
	m_pVideoRenderer->SaveSettings();

	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	return S_OK;
}

HWND CVRMainPPage::CreateHintWindow(HWND parent, int timePop, int timeInit, int timeReshow)
{
	HWND hhint = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);

	SetWindowPos(hhint, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(timePop, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(timeInit, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(timeReshow, 0));
	SendMessageW(hhint, TTM_SETMAXTIPWIDTH, 0, 470);
	return hhint;
}

void CVRMainPPage::AddHint(int id, const LPCWSTR text)
{
	if (!m_hHint) {
		m_hHint = CreateHintWindow(m_Dlg, 15000);
	}
	TOOLINFOW ti;
	ti.cbSize = sizeof(TOOLINFOW);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = m_Dlg;
	ti.uId = (LPARAM)GetDlgItem(id).m_hWnd;
	ti.lpszText = const_cast<LPWSTR>(text);
	SendMessageW(m_hHint, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// CVRPageFlipPPage

CVRPageFlipPPage::CVRPageFlipPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"PageFlipProp", lpunk, IDD_PAGEFLIPPROPPAGE, IDS_PAGEFLIPPROPPAGE_TITLE)
{
	DLog(L"CVRPageFlipPPage()");
}

CVRPageFlipPPage::~CVRPageFlipPPage()
{
	if (m_warnBrush) {
		DeleteObject(m_warnBrush);
		m_warnBrush = nullptr;
	}
}

HWND CVRPageFlipPPage::CreateHintWindow(HWND parent, int timePop, int timeInit, int timeReshow)
{
	HWND hhint = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);

	SetWindowPos(hhint, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(timePop, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(timeInit, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(timeReshow, 0));
	SendMessageW(hhint, TTM_SETMAXTIPWIDTH, 0, 470);
	return hhint;
}

void CVRPageFlipPPage::AddHint(int id, const LPCWSTR text)
{
	if (!m_hHint) {
		m_hHint = CreateHintWindow(m_Dlg, 15000);
	}
	TOOLINFOW ti;
	ti.cbSize = sizeof(TOOLINFOW);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = m_Dlg;
	ti.uId = (LPARAM)GetDlgItem(id).m_hWnd;
	ti.lpszText = const_cast<LPWSTR>(text);
	SendMessageW(m_hHint, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void CVRPageFlipPPage::SetControls()
{
	m_loading = true;

	CheckDlgButton(IDC_PF_ENABLE, m_cfg.enabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PF_FLIP_EYES, m_cfg.flipEyes ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PF_OVERLAY, m_cfg.showOverlay ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PF_CALIBRATION, m_cfg.calibrationMode ? BST_CHECKED : BST_UNCHECKED);

	std::wstring rateStr = L"0";
	if (m_cfg.rateHz > 0.0) {
		rateStr = std::format(L"{:.6g}", m_cfg.rateHz);
	}
	SetDlgItemTextW(IDC_PF_RATE, rateStr.c_str());

	ComboBox_SelectByItemData(m_hWnd, IDC_PF_ASPECT, (LONG_PTR)m_cfg.defaultAspect);
	ComboBox_SelectByItemData(m_hWnd, IDC_PF_LOG_LEVEL, (LONG_PTR)m_cfg.logLevel);
	ComboBox_SelectByItemData(m_hWnd, IDC_PF_WHITE_CORNER, (LONG_PTR)m_cfg.whiteboxCorner);

	SetDlgItemTextW(IDC_PF_DISPLAY_SIZE, std::to_wstring(m_cfg.displaySizeInches).c_str());
	SetDlgItemTextW(IDC_PF_ZOOM, std::to_wstring(m_cfg.displayZoomFactor).c_str());
	std::wstring parallaxStr = L"0";
	if (m_cfg.displayParallax != 0.0) {
		parallaxStr = std::format(L"{:.6g}", m_cfg.displayParallax);
	}
	SetDlgItemTextW(IDC_PF_PARALLAX, parallaxStr.c_str());
	SetDlgItemTextW(IDC_PF_WHITE_BRIGHTNESS, std::to_wstring(m_cfg.whiteboxBrightness).c_str());
	SetDlgItemTextW(IDC_PF_WHITE_VERT, std::to_wstring(m_cfg.whiteboxVerticalPosition).c_str());
	SetDlgItemTextW(IDC_PF_WHITE_HORZ, std::to_wstring(m_cfg.whiteboxHorizontalPosition).c_str());
	SetDlgItemTextW(IDC_PF_WHITE_SIZE, std::to_wstring(m_cfg.whiteboxSize).c_str());
	SetDlgItemTextW(IDC_PF_WHITE_SPACING, std::to_wstring(m_cfg.whiteboxHorizontalSpacing).c_str());
	SetDlgItemTextW(IDC_PF_BLACK_BORDER, std::to_wstring(m_cfg.blackboxBorder).c_str());

	SetDlgItemTextW(IDC_PF_CONFIG_PATH, m_configPath.c_str());
	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	const int driveMode = m_connected ? m_cfg.irDriveMode : 0;
	SendDlgItemMessageW(IDC_EM_DRIVE_MODE, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_EM_DRIVE_MODE, L"0 (optical)", 0);
	ComboBox_AddStringData(m_hWnd, IDC_EM_DRIVE_MODE, L"1 (serial)", 1);
	if (driveMode != 0 && driveMode != 1) {
		const std::wstring label = std::format(L"Other ({})", driveMode);
		ComboBox_AddStringData(m_hWnd, IDC_EM_DRIVE_MODE, label.c_str(), driveMode);
	}
	ComboBox_SelectByItemData(m_hWnd, IDC_EM_DRIVE_MODE, (LONG_PTR)driveMode);

	SetDlgItemTextW(IDC_EM_PROTOCOL, std::to_wstring(m_cfg.irProtocol).c_str());
	SetDlgItemTextW(IDC_EM_FRAME_DELAY, std::to_wstring(m_cfg.irFrameDelay).c_str());
	SetDlgItemTextW(IDC_EM_FRAME_DURATION, std::to_wstring(m_cfg.irFrameDuration).c_str());
	SetDlgItemTextW(IDC_EM_SIGNAL_SPACING, std::to_wstring(m_cfg.irSignalSpacing).c_str());
	SetDlgItemTextW(IDC_EM_FLIP_EYES, std::to_wstring(m_cfg.irFlipEyes).c_str());
	SetDlgItemTextW(IDC_EM_AVG_TIMING, std::to_wstring(m_cfg.irAverageTimingMode).c_str());
	SetDlgItemTextW(IDC_EM_TARGET_FRAMETIME, std::to_wstring(m_cfg.targetFrametime).c_str());
	UpdateTimingWarnings(m_cfg);

	SetDlgItemTextW(IDC_EM_BLOCK_DELAY, std::to_wstring(m_cfg.optBlockSignalDetectionDelay).c_str());
	SetDlgItemTextW(IDC_EM_MIN_THRESHOLD, std::to_wstring(m_cfg.optMinThresholdValueToActivate).c_str());
	SetDlgItemTextW(IDC_EM_THRESH_HIGH, std::to_wstring(m_cfg.optDetectionThresholdHigh).c_str());
	SetDlgItemTextW(IDC_EM_THRESH_LOW, std::to_wstring(m_cfg.optDetectionThresholdLow).c_str());
	SetDlgItemTextW(IDC_EM_IGNORE_DURING_IR, std::to_wstring(m_cfg.optEnableIgnoreDuringIr).c_str());
	SetDlgItemTextW(IDC_EM_DUP_REALTIME, std::to_wstring(m_cfg.optEnableDuplicateRealtimeReporting).c_str());
	SetDlgItemTextW(IDC_EM_OUTPUT_STATS, std::to_wstring(m_cfg.optOutputStats).c_str());
	SetDlgItemTextW(IDC_EM_IGNORE_ALL, std::to_wstring(m_cfg.optIgnoreAllDuplicates).c_str());
	SetDlgItemTextW(IDC_EM_SENSOR_FILTER, std::to_wstring(m_cfg.optSensorFilterMode).c_str());

	if (!m_connected) {
		SetDlgItemTextW(IDC_EM_FIRMWARE, L"Not Connected");
	} else if (m_firmwareVersion > 0) {
		SetDlgItemTextW(IDC_EM_FIRMWARE, std::to_wstring(m_firmwareVersion).c_str());
	} else {
		SetDlgItemTextW(IDC_EM_FIRMWARE, L"Unknown");
	}

	PopulatePorts(m_localSettings.comPort);
	CheckDlgButton(IDC_EM_DISABLE_AUTOCONNECT, m_localSettings.disableAutoConnect ? BST_CHECKED : BST_UNCHECKED);

	GetDlgItem(IDC_EM_CONNECT).EnableWindow(!m_connected);
	GetDlgItem(IDC_EM_DISCONNECT).EnableWindow(m_connected);
	GetDlgItem(IDC_EM_DISABLE_AUTOCONNECT).EnableWindow(TRUE);

	const BOOL enableEmitter = m_connected ? TRUE : FALSE;
	GetDlgItem(IDC_EM_READ).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_APPLY).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SAVE_EEPROM).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_LOAD_JSON).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SAVE_JSON).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_DRIVE_MODE).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_PROTOCOL).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_FRAME_DELAY).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_FRAME_DURATION).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SIGNAL_SPACING).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_FLIP_EYES).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_AVG_TIMING).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_TARGET_FRAMETIME).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_BLOCK_DELAY).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_MIN_THRESHOLD).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_THRESH_HIGH).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_THRESH_LOW).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_IGNORE_DURING_IR).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_DUP_REALTIME).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_OUTPUT_STATS).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_IGNORE_ALL).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SENSOR_FILTER).EnableWindow(enableEmitter);

	bool canUpdateFw = !m_connected;
	int state = State_Stopped;
	if (m_pFilterConfig) {
		if (SUCCEEDED(m_pFilterConfig->Flt_GetInt("playbackState", &state))) {
			canUpdateFw = canUpdateFw && (state == State_Stopped);
		}
	}
	GetDlgItem(IDC_EM_UPDATE_FIRMWARE).EnableWindow(canUpdateFw ? TRUE : FALSE);
	HWND hFwWarn = GetDlgItem(IDC_EM_FW_DISABLED_WARN);
	if (hFwWarn) {
		if (canUpdateFw) {
			::ShowWindow(hFwWarn, SW_HIDE);
		}
		else {
			std::wstring text = L"FW disabled";
			const bool connected = m_connected;
			const bool playing = (state != State_Stopped);
			if (connected && playing) {
				text = L"FW disabled: play+conn";
			}
			else if (connected) {
				text = L"FW disabled: connected";
			}
			else if (playing) {
				text = L"FW disabled: playback";
			}
			SetDlgItemTextW(IDC_EM_FW_DISABLED_WARN, text.c_str());
			::ShowWindow(hFwWarn, SW_SHOW);
		}
	}

	UpdateModeControls();
	UpdateEmitterDirty();

	m_loading = false;
}

static double ParseRateValue(const std::wstring& text, double def)
{
	if (text.empty()) {
		return 0.0;
	}
	wchar_t* end = nullptr;
	const double value = std::wcstod(text.c_str(), &end);
	if (end == text.c_str()) {
		return def;
	}
	return value;
}

static double ParseDoubleValue(const std::wstring& text, double def)
{
	if (text.empty()) {
		return def;
	}
	wchar_t* end = nullptr;
	const double value = std::wcstod(text.c_str(), &end);
	if (end == text.c_str()) {
		return def;
	}
	return value;
}

static int ParseIntValue(const std::wstring& text, int def)
{
	if (text.empty()) {
		return def;
	}
	wchar_t* end = nullptr;
	const long value = std::wcstol(text.c_str(), &end, 10);
	if (end == text.c_str()) {
		return def;
	}
	return (int)value;
}

PageFlipConfig CVRPageFlipPPage::GetConfigFromControls() const
{
	PageFlipConfig cfg = m_cfg;

	cfg.enabled = IsDlgButtonChecked(IDC_PF_ENABLE) == BST_CHECKED;
	cfg.flipEyes = IsDlgButtonChecked(IDC_PF_FLIP_EYES) == BST_CHECKED;
	cfg.showOverlay = IsDlgButtonChecked(IDC_PF_OVERLAY) == BST_CHECKED;
	cfg.calibrationMode = IsDlgButtonChecked(IDC_PF_CALIBRATION) == BST_CHECKED;

	const std::wstring rateText = GetDlgItemTextString(m_hWnd, IDC_PF_RATE);
	cfg.rateHz = ParseRateValue(rateText, cfg.rateHz);
	if (cfg.rateHz < 0.0) {
		cfg.rateHz = 0.0;
	}

	const LONG_PTR aspect = ComboBox_GetCurItemData(m_hWnd, IDC_PF_ASPECT);
	if (aspect != CB_ERR) {
		cfg.defaultAspect = static_cast<PageFlipAspectMode>(aspect);
	}

	const LONG_PTR level = ComboBox_GetCurItemData(m_hWnd, IDC_PF_LOG_LEVEL);
	if (level != CB_ERR) {
		cfg.logLevel = static_cast<PageFlipLogLevel>(level);
	}

	const LONG_PTR corner = ComboBox_GetCurItemData(m_hWnd, IDC_PF_WHITE_CORNER);
	if (corner != CB_ERR) {
		cfg.whiteboxCorner = static_cast<PageFlipCornerPosition>(corner);
	}

	cfg.displaySizeInches = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_DISPLAY_SIZE), cfg.displaySizeInches);
	cfg.displayZoomFactor = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_ZOOM), cfg.displayZoomFactor);
	cfg.displayParallax = ParseDoubleValue(GetDlgItemTextString(m_hWnd, IDC_PF_PARALLAX), cfg.displayParallax);
	cfg.whiteboxBrightness = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_WHITE_BRIGHTNESS), cfg.whiteboxBrightness);
	cfg.whiteboxVerticalPosition = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_WHITE_VERT), cfg.whiteboxVerticalPosition);
	cfg.whiteboxHorizontalPosition = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_WHITE_HORZ), cfg.whiteboxHorizontalPosition);
	cfg.whiteboxSize = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_WHITE_SIZE), cfg.whiteboxSize);
	cfg.whiteboxHorizontalSpacing = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_WHITE_SPACING), cfg.whiteboxHorizontalSpacing);
	cfg.blackboxBorder = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_PF_BLACK_BORDER), cfg.blackboxBorder);

	const LONG_PTR drive = ComboBox_GetCurItemData(m_hWnd, IDC_EM_DRIVE_MODE);
	if (drive != CB_ERR) {
		cfg.irDriveMode = static_cast<int>(drive);
	}

	cfg.comPort = GetSelectedPort();

	cfg.irProtocol = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_PROTOCOL), cfg.irProtocol);
	cfg.irFrameDelay = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_FRAME_DELAY), cfg.irFrameDelay);
	cfg.irFrameDuration = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_FRAME_DURATION), cfg.irFrameDuration);
	cfg.irSignalSpacing = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_SIGNAL_SPACING), cfg.irSignalSpacing);
	cfg.irFlipEyes = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_FLIP_EYES), cfg.irFlipEyes);
	cfg.irAverageTimingMode = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_AVG_TIMING), cfg.irAverageTimingMode);
	cfg.targetFrametime = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_TARGET_FRAMETIME), cfg.targetFrametime);

	cfg.optBlockSignalDetectionDelay = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_BLOCK_DELAY), cfg.optBlockSignalDetectionDelay);
	cfg.optMinThresholdValueToActivate = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_MIN_THRESHOLD), cfg.optMinThresholdValueToActivate);
	cfg.optDetectionThresholdHigh = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_THRESH_HIGH), cfg.optDetectionThresholdHigh);
	cfg.optDetectionThresholdLow = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_THRESH_LOW), cfg.optDetectionThresholdLow);
	cfg.optEnableIgnoreDuringIr = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_IGNORE_DURING_IR), cfg.optEnableIgnoreDuringIr);
	cfg.optEnableDuplicateRealtimeReporting = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_DUP_REALTIME), cfg.optEnableDuplicateRealtimeReporting);
	cfg.optOutputStats = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_OUTPUT_STATS), cfg.optOutputStats);
	cfg.optIgnoreAllDuplicates = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_IGNORE_ALL), cfg.optIgnoreAllDuplicates);
	cfg.optSensorFilterMode = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_SENSOR_FILTER), cfg.optSensorFilterMode);

	return cfg;
}

void CVRPageFlipPPage::PopulatePorts(const std::wstring& selectedPort)
{
	m_ports = EnumeratePageFlipPorts();
	m_missingPort.clear();

	SendDlgItemMessageW(IDC_EM_COMPORT, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_EM_COMPORT, L"AUTO", -1);

	int selectedIndex = 0;
	for (size_t i = 0; i < m_ports.size(); ++i) {
		std::wstring label = m_ports[i].port;
		if (!m_ports[i].description.empty()) {
			label.append(L" - ").append(m_ports[i].description);
		}
		ComboBox_AddStringData(m_hWnd, IDC_EM_COMPORT, label.c_str(), (LONG_PTR)i);
		if (!selectedPort.empty() && _wcsicmp(selectedPort.c_str(), m_ports[i].port.c_str()) == 0) {
			selectedIndex = (int)i + 1;
		}
	}

	if (selectedPort.empty()) {
		selectedIndex = 0;
	} else if (selectedIndex == 0) {
		std::wstring missing = selectedPort;
		missing.append(L" (missing)");
		ComboBox_AddStringData(m_hWnd, IDC_EM_COMPORT, missing.c_str(), -2);
		m_missingPort = selectedPort;
		selectedIndex = (int)SendDlgItemMessageW(IDC_EM_COMPORT, CB_GETCOUNT, 0, 0) - 1;
	}

	SendDlgItemMessageW(IDC_EM_COMPORT, CB_SETCURSEL, selectedIndex, 0);
}

std::wstring CVRPageFlipPPage::GetSelectedPort() const
{
	const LONG_PTR data = ComboBox_GetCurItemData(m_hWnd, IDC_EM_COMPORT);
	if (data == -1 || data == CB_ERR) {
		return {};
	}
	if (data == -2) {
		return m_missingPort;
	}
	if (data >= 0 && (size_t)data < m_ports.size()) {
		return m_ports[data].port;
	}
	return {};
}

void CVRPageFlipPPage::UpdateEmitterState()
{
	if (!m_pFilterConfig) {
		return;
	}

	PageFlipEmitterStateBin* state = nullptr;
	unsigned size = 0;
	if (SUCCEEDED(m_pFilterConfig->Flt_GetBin("pageflip_emitter_state", (LPVOID*)&state, &size)) && state && size == sizeof(PageFlipEmitterStateBin)) {
		m_cfg = FromPageFlipConfigBin(state->config, m_cfg);
		m_connected = state->connected != 0;
		m_firmwareVersion = state->firmwareVersion;
		LocalFree(state);
	}
}

void CVRPageFlipPPage::UpdateLocalEmitterSettings()
{
	if (!m_pFilterConfig) {
		m_localSettings = LoadLocalEmitterSettings(m_configPath);
		return;
	}

	LocalEmitterSettingsBin* bin = nullptr;
	unsigned size = 0;
	if (SUCCEEDED(m_pFilterConfig->Flt_GetBin("pageflip_emitter_local_settings", (LPVOID*)&bin, &size))
			&& bin && size == sizeof(LocalEmitterSettingsBin)) {
		m_localSettings = FromLocalEmitterSettingsBin(*bin);
		LocalFree(bin);
		return;
	}

	m_localSettings = LoadLocalEmitterSettings(m_configPath);
}

void CVRPageFlipPPage::ApplyLocalEmitterSettings(bool disableAutoConnect, const std::wstring& comPort)
{
	m_localSettings.disableAutoConnect = disableAutoConnect;
	m_localSettings.comPort = comPort;

	if (!m_pFilterConfig) {
		return;
	}

	LocalEmitterSettingsBin bin = ToLocalEmitterSettingsBin(m_localSettings);
	m_pFilterConfig->Flt_SetBin("pageflip_emitter_local_settings", &bin, sizeof(bin));
}

void CVRPageFlipPPage::SetOpticalControlsEnabled(bool enabled)
{
	const int displayIds[] = {
		IDC_PF_DISPLAY_SIZE_LABEL,
		IDC_PF_DISPLAY_SIZE,
		IDC_PF_WHITE_BRIGHTNESS_LABEL,
		IDC_PF_WHITE_BRIGHTNESS,
		IDC_PF_WHITE_CORNER_LABEL,
		IDC_PF_WHITE_CORNER,
		IDC_PF_WHITE_VERT_LABEL,
		IDC_PF_WHITE_VERT,
		IDC_PF_WHITE_HORZ_LABEL,
		IDC_PF_WHITE_HORZ,
		IDC_PF_WHITE_SIZE_LABEL,
		IDC_PF_WHITE_SIZE,
		IDC_PF_WHITE_SPACING_LABEL,
		IDC_PF_WHITE_SPACING,
		IDC_PF_BLACK_BORDER_LABEL,
		IDC_PF_BLACK_BORDER
	};

	const int emitterIds[] = {
		IDC_EM_BLOCK_DELAY_LABEL,
		IDC_EM_BLOCK_DELAY,
		IDC_EM_MIN_THRESHOLD_LABEL,
		IDC_EM_MIN_THRESHOLD,
		IDC_EM_THRESH_HIGH_LABEL,
		IDC_EM_THRESH_HIGH,
		IDC_EM_THRESH_LOW_LABEL,
		IDC_EM_THRESH_LOW,
		IDC_EM_IGNORE_DURING_IR_LABEL,
		IDC_EM_IGNORE_DURING_IR,
		IDC_EM_DUP_REALTIME_LABEL,
		IDC_EM_DUP_REALTIME,
		IDC_EM_OUTPUT_STATS_LABEL,
		IDC_EM_OUTPUT_STATS,
		IDC_EM_IGNORE_ALL_LABEL,
		IDC_EM_IGNORE_ALL,
		IDC_EM_SENSOR_FILTER_LABEL,
		IDC_EM_SENSOR_FILTER
	};

	const BOOL enable = enabled ? TRUE : FALSE;
	for (int id : displayIds) {
		const HWND h = GetDlgItem(id);
		if (h) {
			::EnableWindow(h, enable);
		}
	}
	for (int id : emitterIds) {
		const HWND h = GetDlgItem(id);
		if (h) {
			::EnableWindow(h, enable);
		}
	}
}

void CVRPageFlipPPage::UpdateModeControls()
{
	const int driveMode = m_connected ? m_cfg.irDriveMode : 0;
	const bool serialMode = driveMode == 1;
	const bool opticalMode = driveMode == 0;
	const bool otherMode = !serialMode && !opticalMode;

	const BOOL enableSelect = m_connected ? TRUE : FALSE;
	GetDlgItem(IDC_PF_MODE_SERIAL).EnableWindow(enableSelect);
	GetDlgItem(IDC_PF_MODE_OPTICAL).EnableWindow(enableSelect);
	GetDlgItem(IDC_PF_MODE_OTHER).EnableWindow(FALSE);
	CWindow modeNote = GetDlgItem(IDC_PF_MODE_NOTE);
	if (modeNote) {
		modeNote.ShowWindow(m_connected ? SW_HIDE : SW_SHOW);
	}

	const int checkId = otherMode ? IDC_PF_MODE_OTHER : (serialMode ? IDC_PF_MODE_SERIAL : IDC_PF_MODE_OPTICAL);
	::CheckRadioButton(m_hWnd, IDC_PF_MODE_SERIAL, IDC_PF_MODE_OTHER, checkId);

	SetOpticalControlsEnabled(!serialMode);
}

void CVRPageFlipPPage::UpdateEmitterDirty()
{
	bool dirty = false;
	if (m_pFilterConfig) {
		m_pFilterConfig->Flt_GetBool("pageflip_emitter_dirty", &dirty);
	}
	m_emitterDirty = dirty;
	HWND hWarn = GetDlgItem(IDC_EM_EEPROM_WARN);
	if (hWarn) {
		const bool showWarn = m_emitterDirty || m_emitterPending;
		if (showWarn) {
			const wchar_t* text = L"Emitter settings not saved to EEPROM.";
			if (m_emitterPending && m_emitterDirty) {
				text = L"Settings not applied to emitter; not saved to EEPROM.";
			}
			else if (m_emitterPending) {
				text = L"Settings not applied to emitter.";
			}
			SetDlgItemTextW(IDC_EM_EEPROM_WARN, text);
			::ShowWindow(hWarn, SW_SHOW);
		}
		else {
			::ShowWindow(hWarn, SW_HIDE);
		}
	}
}

void CVRPageFlipPPage::UpdateTimingWarnings(const PageFlipConfig& cfg)
{
	const bool hasTarget = cfg.targetFrametime > 0;
	const bool avgEnabled = cfg.irAverageTimingMode != 0;

	HWND hAvgTiming = GetDlgItem(IDC_EM_AVG_TIMING);
	if (hAvgTiming) {
		::EnableWindow(hAvgTiming, hasTarget ? TRUE : FALSE);
	}

	HWND hAvgWarn = GetDlgItem(IDC_EM_AVG_REQ_WARN);
	if (hAvgWarn) {
		::ShowWindow(hAvgWarn, hasTarget ? SW_HIDE : SW_SHOW);
	}

	const double refreshHz = GetPrimaryDisplayRefreshHz();
	bool showTargetWarn = false;
	std::wstring warnText = L"Display refresh differs.";
	if (hasTarget && refreshHz > 0.0) {
		const double expectedUs = 1000000.0 / refreshHz;
		const double diff = std::fabs(expectedUs - (double)cfg.targetFrametime);
		const double threshold = std::max(100.0, expectedUs * 0.005);
		if (diff > threshold) {
			showTargetWarn = true;
			warnText = std::format(L"Display ~{:.3f} Hz mismatch", refreshHz);
		}
	}

	HWND hTargetWarn = GetDlgItem(IDC_EM_TARGET_WARN);
	if (hTargetWarn) {
		if (showTargetWarn) {
			SetDlgItemTextW(IDC_EM_TARGET_WARN, warnText.c_str());
			::ShowWindow(hTargetWarn, SW_SHOW);
		}
		else {
			::ShowWindow(hTargetWarn, SW_HIDE);
		}
	}

	m_targetWarnRed = showTargetWarn && avgEnabled;
	if (hTargetWarn) {
		::InvalidateRect(hTargetWarn, nullptr, TRUE);
	}
	if (hAvgWarn) {
		::InvalidateRect(hAvgWarn, nullptr, TRUE);
	}
}

HRESULT CVRPageFlipPPage::OnConnect(IUnknown* pUnknown)
{
	if (pUnknown == nullptr) {
		return E_POINTER;
	}

	m_pVideoRenderer = pUnknown;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}
	m_pFilterConfig = pUnknown;
	if (!m_pFilterConfig) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRPageFlipPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pFilterConfig.Release();
	m_pVideoRenderer.Release();
	return S_OK;
}

HRESULT CVRPageFlipPPage::OnActivate()
{
	m_hWnd = m_hwnd;
	m_configPath = GetDefaultPageFlipConfigPath();
	m_cfg = LoadPageFlipConfig(m_configPath);
	m_emitterPending = false;

	SendDlgItemMessageW(IDC_PF_ASPECT, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_PF_ASPECT, L"side-by-side", (LONG_PTR)PageFlipAspectMode::SideBySide);
	ComboBox_AddStringData(m_hWnd, IDC_PF_ASPECT, L"top-and-bottom", (LONG_PTR)PageFlipAspectMode::TopAndBottom);

	SendDlgItemMessageW(IDC_PF_LOG_LEVEL, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_PF_LOG_LEVEL, L"ERROR", (LONG_PTR)PageFlipLogLevel::Error);
	ComboBox_AddStringData(m_hWnd, IDC_PF_LOG_LEVEL, L"WARNING", (LONG_PTR)PageFlipLogLevel::Warning);
	ComboBox_AddStringData(m_hWnd, IDC_PF_LOG_LEVEL, L"INFO", (LONG_PTR)PageFlipLogLevel::Info);
	ComboBox_AddStringData(m_hWnd, IDC_PF_LOG_LEVEL, L"DEBUG", (LONG_PTR)PageFlipLogLevel::Debug);

	SendDlgItemMessageW(IDC_PF_WHITE_CORNER, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_PF_WHITE_CORNER, L"top_left", (LONG_PTR)PageFlipCornerPosition::TopLeft);
	ComboBox_AddStringData(m_hWnd, IDC_PF_WHITE_CORNER, L"top_right", (LONG_PTR)PageFlipCornerPosition::TopRight);
	ComboBox_AddStringData(m_hWnd, IDC_PF_WHITE_CORNER, L"bottom_left", (LONG_PTR)PageFlipCornerPosition::BottomLeft);
	ComboBox_AddStringData(m_hWnd, IDC_PF_WHITE_CORNER, L"bottom_right", (LONG_PTR)PageFlipCornerPosition::BottomRight);

	UpdateEmitterState();
	if (m_connected && m_pFilterConfig) {
		m_pFilterConfig->Flt_SetBool("pageflip_emitter_refresh", true);
		UpdateEmitterState();
	}
	UpdateLocalEmitterSettings();

	SetControls();

	if (!m_warnBrush) {
		m_warnBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	}

	AddHint(IDC_PF_MODE_SERIAL, L"Use PC serial drive mode (recommended). Updating this sends the drive mode to the emitter.");
	AddHint(IDC_PF_MODE_OPTICAL, L"Use optical sensor drive mode (standalone). Updating this sends the drive mode to the emitter.");

	AddHint(IDC_PF_ENABLE, L"Enable software pageflipping output. Toggle with Ctrl+Shift+F8.");
	AddHint(IDC_PF_RATE, L"Total flip rate in Hz. Use 0 to follow display refresh.");
	AddHint(IDC_PF_ASPECT, L"Default aspect for half side-by-side or half top-and-bottom sources.");
	AddHint(IDC_PF_FLIP_EYES, L"Swap which eye is shown for left/right. Toggle with Ctrl+Shift+F12.");
	AddHint(IDC_PF_OVERLAY, L"Show status OSD text. Toggle with Ctrl+Shift+F9.");
	AddHint(IDC_PF_LOG_LEVEL, L"Logging level for the pageflip log.");
	AddHint(IDC_PF_DISPLAY_SIZE, L"Display size in inches; scales trigger boxes to match sensor bar.");
	AddHint(IDC_PF_ZOOM, L"(0-100) Zooms out the video to shrink for displays with ghosting.");
	AddHint(IDC_PF_PARALLAX, L"Horizontal eye shift in percent of the output width. Positive pushes left eye left and right eye right. Supports fractional values.");
	AddHint(IDC_PF_WHITE_BRIGHTNESS, L"Whitebox brightness from 0 (black) to 255 (white).");
	AddHint(IDC_PF_WHITE_CORNER, L"Position of the trigger boxes: top_left, top_right, bottom_left, bottom_right.");
	AddHint(IDC_PF_WHITE_VERT, L"Whitebox vertical position; roughly mm when display size is correct.");
	AddHint(IDC_PF_WHITE_HORZ, L"Whitebox horizontal position; roughly mm when display size is correct.");
	AddHint(IDC_PF_WHITE_SIZE, L"Whitebox size; too large causes crosstalk, too small misses triggers.");
	AddHint(IDC_PF_WHITE_SPACING, L"Spacing between the two whiteboxes; roughly mm when display size is correct.");
	AddHint(IDC_PF_BLACK_BORDER, L"Width of black border that blocks video content from the trigger boxes.");
	AddHint(IDC_PF_CALIBRATION, L"Calibration mode shows reticle and enables hotkey tuning of timing. Toggle with Ctrl+Shift+F10.");
	AddHint(IDC_PF_LOAD, L"Load display settings JSON.");
	AddHint(IDC_PF_SAVE, L"Save display settings JSON.");

	AddHint(IDC_EM_COMPORT, L"Used as the serial port when connecting to the emitter. On Windows use COMx.");
	AddHint(IDC_EM_REFRESH, L"Refresh the list of available COM ports.");
	AddHint(IDC_EM_CONNECT, L"Connect to the emitter on the selected serial port.");
	AddHint(IDC_EM_DISCONNECT, L"Disconnect from the emitter.");
	AddHint(IDC_EM_DISABLE_AUTOCONNECT, L"Disable automatic scanning and reconnection attempts.");
	AddHint(IDC_EM_FIRMWARE, L"The version of your IR emitter firmware.");
	AddHint(IDC_EM_DRIVE_MODE, L"0=Optical, 1=PCSerial. Calibration hotkey: T toggles drive mode. (Firmware 20+ for drive mode setting.)");
	AddHint(IDC_EM_PROTOCOL, L"0=Samsung07, 1=Xpand, 2=3DVision, 3=Sharp, 4=Sony, 5=Panasonic, 6=PanasonicCustom, 7=DLPLink.");
	AddHint(IDC_EM_FRAME_DELAY, L"(us) Delay after signal before activating glasses.");
	AddHint(IDC_EM_FRAME_DURATION, L"(us) Duration to keep glasses active after activation.");
	AddHint(IDC_EM_SIGNAL_SPACING, L"(us) Delay between IR signals to avoid overloading receiver.");
	AddHint(IDC_EM_FLIP_EYES, L"Set to 1 to flip left/right eye signals.");
	AddHint(IDC_EM_AVG_TIMING, L"0=Disable, 1=Mode 1 (averaging to reduce jitter).");
	AddHint(IDC_EM_TARGET_FRAMETIME, L"(us) Expected frame time (1,000,000 / refresh rate). Used for averaging.");
	AddHint(IDC_EM_BLOCK_DELAY, L"(us) Block detection after a trigger to avoid false repeats.");
	AddHint(IDC_EM_IGNORE_ALL, L"Ignore all duplicate detections (0=off, 1=on).");
	AddHint(IDC_EM_MIN_THRESHOLD, L"(1-255) Min intensity required before emitter turns on.");
	AddHint(IDC_EM_THRESH_HIGH, L"(1-255) Rising threshold to detect new frame start.");
	AddHint(IDC_EM_THRESH_LOW, L"(1-255) Falling threshold required before accepting next trigger.");
	AddHint(IDC_EM_IGNORE_DURING_IR, L"Disable opt detection during IR emission to avoid false triggers.");
	AddHint(IDC_EM_DUP_REALTIME, L"Emit realtime duplicate reports over serial (0=off, 1=on).");
	AddHint(IDC_EM_OUTPUT_STATS, L"Output opt module stats over serial (0=off, 1=on).");
	AddHint(IDC_EM_SENSOR_FILTER, L"Sensor filter mode (0=off, 1=mode 1).");
	AddHint(IDC_EM_READ, L"Read current settings from the emitter.");
	AddHint(IDC_EM_APPLY, L"Apply settings to the emitter.");
	AddHint(IDC_EM_SAVE_EEPROM, L"Save current emitter settings to EEPROM. Calibration hotkey: B saves to EEPROM.");
	AddHint(IDC_EM_LOAD_JSON, L"Load emitter settings from JSON.");
	AddHint(IDC_EM_SAVE_JSON, L"Save emitter settings to JSON.");
	AddHint(IDC_EM_UPDATE_FIRMWARE, L"Open firmware update dialog. Disabled while playback is active or the emitter is connected.");

	return S_OK;
}

INT_PTR CVRPageFlipPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CTLCOLORSTATIC) {
		const HWND hCtl = (HWND)lParam;
		if (hCtl == GetDlgItem(IDC_EM_EEPROM_WARN) || hCtl == GetDlgItem(IDC_PF_MODE_NOTE)) {
			HDC hdc = (HDC)wParam;
			SetTextColor(hdc, RGB(200, 0, 0));
			SetBkMode(hdc, TRANSPARENT);
			return (INT_PTR)(m_warnBrush ? m_warnBrush : GetSysColorBrush(COLOR_BTNFACE));
		}
		if (hCtl == GetDlgItem(IDC_EM_TARGET_WARN)) {
			HDC hdc = (HDC)wParam;
			SetTextColor(hdc, m_targetWarnRed ? RGB(200, 0, 0) : RGB(0, 0, 0));
			SetBkMode(hdc, TRANSPARENT);
			return (INT_PTR)(m_warnBrush ? m_warnBrush : GetSysColorBrush(COLOR_BTNFACE));
		}
		if (hCtl == GetDlgItem(IDC_EM_AVG_REQ_WARN)) {
			HDC hdc = (HDC)wParam;
			SetTextColor(hdc, RGB(0, 0, 0));
			SetBkMode(hdc, TRANSPARENT);
			return (INT_PTR)(m_warnBrush ? m_warnBrush : GetSysColorBrush(COLOR_BTNFACE));
		}
	}

	if (uMsg == WM_COMMAND) {
		const int nID = LOWORD(wParam);
		const int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			if (nID == IDC_PF_LOAD) {
				std::wstring path;
				if (BrowseOpenFile(m_hWnd, L"Display Settings (*.display_settings.json)\0*.display_settings.json\0All Files\0*.*\0", L"display_settings.json", path)) {
					m_cfg = LoadPageFlipConfig(path);
					SetControls();
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_PF_SAVE) {
				std::wstring path;
				if (BrowseSaveFile(m_hWnd, L"Display Settings (*.display_settings.json)\0*.display_settings.json\0All Files\0*.*\0", L"display_settings.json", path)) {
					PageFlipConfig cfg = GetConfigFromControls();
					SavePageFlipConfig(path, cfg);
				}
				return (LRESULT)1;
			}

			if (nID == IDC_PF_ENABLE || nID == IDC_PF_FLIP_EYES || nID == IDC_PF_OVERLAY || nID == IDC_PF_CALIBRATION) {
				if (!m_loading) {
					SetDirty();
				}
				return (LRESULT)1;
			}

			if ((nID == IDC_PF_MODE_SERIAL || nID == IDC_PF_MODE_OPTICAL) && m_pFilterConfig) {
				if (!m_loading) {
					if (!m_connected) {
						UpdateModeControls();
						return (LRESULT)1;
					}
					const int newMode = (nID == IDC_PF_MODE_SERIAL) ? 1 : 0;
					PageFlipConfig cfg = GetConfigFromControls();
					cfg.irDriveMode = newMode;
					PageFlipConfigBin bin = ToPageFlipConfigBin(cfg);
					m_pFilterConfig->Flt_SetBin("pageflip_emitter_apply", &bin, sizeof(bin));
					m_cfg = cfg;
					m_emitterPending = false;
					SetDirty();
					UpdateEmitterState();
					UpdateLocalEmitterSettings();
					SetControls();
				}
				return (LRESULT)1;
			}

			if (nID == IDC_EM_REFRESH) {
				const std::wstring current = GetSelectedPort();
				PopulatePorts(current);
				return (LRESULT)1;
			}
			if (nID == IDC_EM_CONNECT && m_pFilterConfig) {
				ApplyLocalEmitterSettings(false, GetSelectedPort());
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_connect", true);
				UpdateEmitterState();
				UpdateLocalEmitterSettings();
				m_emitterPending = false;
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_DISCONNECT && m_pFilterConfig) {
				ApplyLocalEmitterSettings(true, GetSelectedPort());
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_connect", false);
				UpdateEmitterState();
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_DISABLE_AUTOCONNECT && m_pFilterConfig) {
				const bool disable = IsDlgButtonChecked(IDC_EM_DISABLE_AUTOCONNECT) == BST_CHECKED;
				ApplyLocalEmitterSettings(disable, GetSelectedPort());
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_READ && m_pFilterConfig) {
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_refresh", true);
				UpdateEmitterState();
				m_emitterPending = false;
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_APPLY && m_pFilterConfig) {
				PageFlipConfig cfg = GetConfigFromControls();
				PageFlipConfigBin bin = ToPageFlipConfigBin(cfg);
				m_pFilterConfig->Flt_SetBin("pageflip_emitter_apply", &bin, sizeof(bin));
				m_cfg = cfg;
				m_emitterPending = false;
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_SAVE_EEPROM && m_pFilterConfig) {
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_save", true);
				UpdateEmitterDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_LOAD_JSON) {
				std::wstring path;
				if (BrowseOpenFile(m_hWnd, L"Emitter Settings (*.emitter_settings.json)\0*.emitter_settings.json\0All Files\0*.*\0", L"emitter_settings.json", path)) {
					PageFlipConfig cfg = m_cfg;
					if (LoadEmitterSettingsJson(path, cfg)) {
						m_cfg = cfg;
						m_emitterPending = true;
						SetControls();
						SetDirty();
					}
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_SAVE_JSON) {
				std::wstring path;
				if (BrowseSaveFile(m_hWnd, L"Emitter Settings (*.emitter_settings.json)\0*.emitter_settings.json\0All Files\0*.*\0", L"emitter_settings.json", path)) {
					PageFlipConfig cfg = GetConfigFromControls();
					SaveEmitterSettingsJson(path, cfg);
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_UPDATE_FIRMWARE) {
				if (!m_pFilterConfig) {
					::MessageBoxW(m_hWnd, L"Unable to access renderer state.", L"Firmware Update", MB_ICONERROR | MB_OK);
					return (LRESULT)1;
				}

				UpdateEmitterState();
				UpdateLocalEmitterSettings();

				if (m_connected) {
					::MessageBoxW(m_hWnd, L"Disconnect from the emitter before updating firmware.", L"Firmware Update", MB_ICONWARNING | MB_OK);
					return (LRESULT)1;
				}

				int state = State_Stopped;
				if (FAILED(m_pFilterConfig->Flt_GetInt("playbackState", &state)) || state != State_Stopped) {
					::MessageBoxW(m_hWnd, L"Stop playback before updating firmware.", L"Firmware Update", MB_ICONWARNING | MB_OK);
					return (LRESULT)1;
				}

				const LocalEmitterSettings prevSettings = m_localSettings;
				if (!m_localSettings.disableAutoConnect) {
					ApplyLocalEmitterSettings(true, prevSettings.comPort);
					UpdateLocalEmitterSettings();
				}

				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_CONNECT), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_DISCONNECT), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_COMPORT), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_REFRESH), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_DISABLE_AUTOCONNECT), FALSE);

				EmitterFirmwareDialog dialog(m_hWnd);
				dialog.ShowModal();

				ApplyLocalEmitterSettings(prevSettings.disableAutoConnect, prevSettings.comPort);
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
		}

		if (action == CBN_SELCHANGE) {
			if (nID == IDC_PF_ASPECT || nID == IDC_PF_LOG_LEVEL || nID == IDC_PF_WHITE_CORNER) {
				if (!m_loading) {
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_COMPORT) {
				if (!m_loading) {
					ApplyLocalEmitterSettings(m_localSettings.disableAutoConnect, GetSelectedPort());
					UpdateLocalEmitterSettings();
					SetControls();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_DRIVE_MODE) {
				if (!m_loading) {
					const LONG_PTR drive = ComboBox_GetCurItemData(m_hWnd, IDC_EM_DRIVE_MODE);
					if (drive != CB_ERR) {
						m_cfg.irDriveMode = static_cast<int>(drive);
					}
					UpdateModeControls();
					m_emitterPending = true;
					UpdateEmitterDirty();
					SetDirty();
				}
				return (LRESULT)1;
			}
		}

		if (action == EN_CHANGE) {
			const bool isEmitterField = (nID == IDC_EM_PROTOCOL || nID == IDC_EM_FRAME_DELAY || nID == IDC_EM_FRAME_DURATION
					|| nID == IDC_EM_SIGNAL_SPACING || nID == IDC_EM_FLIP_EYES || nID == IDC_EM_AVG_TIMING
					|| nID == IDC_EM_TARGET_FRAMETIME || nID == IDC_EM_BLOCK_DELAY || nID == IDC_EM_MIN_THRESHOLD
					|| nID == IDC_EM_THRESH_HIGH || nID == IDC_EM_THRESH_LOW || nID == IDC_EM_IGNORE_DURING_IR
					|| nID == IDC_EM_DUP_REALTIME || nID == IDC_EM_OUTPUT_STATS || nID == IDC_EM_IGNORE_ALL
					|| nID == IDC_EM_SENSOR_FILTER);
			if (nID == IDC_PF_RATE || nID == IDC_PF_DISPLAY_SIZE || nID == IDC_PF_ZOOM || nID == IDC_PF_PARALLAX
					|| nID == IDC_PF_WHITE_BRIGHTNESS || nID == IDC_PF_WHITE_VERT || nID == IDC_PF_WHITE_HORZ
					|| nID == IDC_PF_WHITE_SIZE || nID == IDC_PF_WHITE_SPACING || nID == IDC_PF_BLACK_BORDER
					|| isEmitterField) {
				if (!m_loading) {
					SetDirty();
					if (isEmitterField) {
						m_emitterPending = true;
						UpdateEmitterDirty();
					}
					if (nID == IDC_EM_AVG_TIMING || nID == IDC_EM_TARGET_FRAMETIME) {
						UpdateTimingWarnings(GetConfigFromControls());
					}
				}
				return (LRESULT)1;
			}
		}
	}

	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRPageFlipPPage::OnApplyChanges()
{
	PageFlipConfig cfg = GetConfigFromControls();
	SavePageFlipConfig(m_configPath, cfg);
	m_cfg = cfg;
	if (m_pFilterConfig) {
		PageFlipConfigBin bin = ToPageFlipConfigBin(cfg);
		m_pFilterConfig->Flt_SetBin("pageflip_apply_config", &bin, sizeof(bin));
	}
	UpdateTimingWarnings(m_cfg);
	return S_OK;
}

// CVREmitterPPage

CVREmitterPPage::CVREmitterPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"EmitterProp", lpunk, IDD_EMITTERPROPPAGE, IDS_EMITTERPROPPAGE_TITLE)
{
	DLog(L"CVREmitterPPage()");
}

HWND CVREmitterPPage::CreateHintWindow(HWND parent, int timePop, int timeInit, int timeReshow)
{
	HWND hhint = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);

	SetWindowPos(hhint, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(timePop, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(timeInit, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(timeReshow, 0));
	SendMessageW(hhint, TTM_SETMAXTIPWIDTH, 0, 470);
	return hhint;
}

void CVREmitterPPage::AddHint(int id, const LPCWSTR text)
{
	if (!m_hHint) {
		m_hHint = CreateHintWindow(m_Dlg, 15000);
	}
	TOOLINFOW ti;
	ti.cbSize = sizeof(TOOLINFOW);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = m_Dlg;
	ti.uId = (LPARAM)GetDlgItem(id).m_hWnd;
	ti.lpszText = const_cast<LPWSTR>(text);
	SendMessageW(m_hHint, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void CVREmitterPPage::SetControls()
{
	m_loading = true;

	const int driveMode = m_connected ? m_cfg.irDriveMode : 0;
	ComboBox_SelectByItemData(m_hWnd, IDC_EM_DRIVE_MODE, (LONG_PTR)driveMode);
	SetDlgItemTextW(IDC_EM_PROTOCOL, std::to_wstring(m_cfg.irProtocol).c_str());
	SetDlgItemTextW(IDC_EM_FRAME_DELAY, std::to_wstring(m_cfg.irFrameDelay).c_str());
	SetDlgItemTextW(IDC_EM_FRAME_DURATION, std::to_wstring(m_cfg.irFrameDuration).c_str());
	SetDlgItemTextW(IDC_EM_SIGNAL_SPACING, std::to_wstring(m_cfg.irSignalSpacing).c_str());
	SetDlgItemTextW(IDC_EM_FLIP_EYES, std::to_wstring(m_cfg.irFlipEyes).c_str());
	SetDlgItemTextW(IDC_EM_AVG_TIMING, std::to_wstring(m_cfg.irAverageTimingMode).c_str());
	SetDlgItemTextW(IDC_EM_TARGET_FRAMETIME, std::to_wstring(m_cfg.targetFrametime).c_str());

	SetDlgItemTextW(IDC_EM_BLOCK_DELAY, std::to_wstring(m_cfg.optBlockSignalDetectionDelay).c_str());
	SetDlgItemTextW(IDC_EM_MIN_THRESHOLD, std::to_wstring(m_cfg.optMinThresholdValueToActivate).c_str());
	SetDlgItemTextW(IDC_EM_THRESH_HIGH, std::to_wstring(m_cfg.optDetectionThresholdHigh).c_str());
	SetDlgItemTextW(IDC_EM_THRESH_LOW, std::to_wstring(m_cfg.optDetectionThresholdLow).c_str());
	SetDlgItemTextW(IDC_EM_IGNORE_DURING_IR, std::to_wstring(m_cfg.optEnableIgnoreDuringIr).c_str());
	SetDlgItemTextW(IDC_EM_DUP_REALTIME, std::to_wstring(m_cfg.optEnableDuplicateRealtimeReporting).c_str());
	SetDlgItemTextW(IDC_EM_OUTPUT_STATS, std::to_wstring(m_cfg.optOutputStats).c_str());
	SetDlgItemTextW(IDC_EM_IGNORE_ALL, std::to_wstring(m_cfg.optIgnoreAllDuplicates).c_str());
	SetDlgItemTextW(IDC_EM_SENSOR_FILTER, std::to_wstring(m_cfg.optSensorFilterMode).c_str());

	if (!m_connected) {
		SetDlgItemTextW(IDC_EM_FIRMWARE, L"Not Connected");
	} else if (m_firmwareVersion > 0) {
		SetDlgItemTextW(IDC_EM_FIRMWARE, std::to_wstring(m_firmwareVersion).c_str());
	} else {
		SetDlgItemTextW(IDC_EM_FIRMWARE, L"Unknown");
	}

	PopulatePorts(m_localSettings.comPort);
	CheckDlgButton(IDC_EM_DISABLE_AUTOCONNECT, m_localSettings.disableAutoConnect ? BST_CHECKED : BST_UNCHECKED);

	GetDlgItem(IDC_EM_CONNECT).EnableWindow(!m_connected);
	GetDlgItem(IDC_EM_DISCONNECT).EnableWindow(m_connected);
	GetDlgItem(IDC_EM_DISABLE_AUTOCONNECT).EnableWindow(TRUE);

	const BOOL enableEmitter = m_connected ? TRUE : FALSE;
	GetDlgItem(IDC_EM_READ).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_APPLY).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SAVE_EEPROM).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_LOAD_JSON).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SAVE_JSON).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_DRIVE_MODE).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_PROTOCOL).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_FRAME_DELAY).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_FRAME_DURATION).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SIGNAL_SPACING).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_FLIP_EYES).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_AVG_TIMING).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_TARGET_FRAMETIME).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_BLOCK_DELAY).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_MIN_THRESHOLD).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_THRESH_HIGH).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_THRESH_LOW).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_IGNORE_DURING_IR).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_DUP_REALTIME).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_OUTPUT_STATS).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_IGNORE_ALL).EnableWindow(enableEmitter);
	GetDlgItem(IDC_EM_SENSOR_FILTER).EnableWindow(enableEmitter);
	bool canUpdateFw = !m_connected;
	if (m_pFilterConfig) {
		int state = State_Stopped;
		if (SUCCEEDED(m_pFilterConfig->Flt_GetInt("playbackState", &state))) {
			canUpdateFw = canUpdateFw && (state == State_Stopped);
		}
	}
	GetDlgItem(IDC_EM_UPDATE_FIRMWARE).EnableWindow(canUpdateFw ? TRUE : FALSE);

	m_loading = false;
}

void CVREmitterPPage::PopulatePorts(const std::wstring& selectedPort)
{
	m_ports = EnumeratePageFlipPorts();
	m_missingPort.clear();

	SendDlgItemMessageW(IDC_EM_COMPORT, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_EM_COMPORT, L"AUTO", -1);

	int selectedIndex = 0;
	for (size_t i = 0; i < m_ports.size(); ++i) {
		std::wstring label = m_ports[i].port;
		if (!m_ports[i].description.empty()) {
			label.append(L" - ").append(m_ports[i].description);
		}
		ComboBox_AddStringData(m_hWnd, IDC_EM_COMPORT, label.c_str(), (LONG_PTR)i);
		if (!selectedPort.empty() && _wcsicmp(selectedPort.c_str(), m_ports[i].port.c_str()) == 0) {
			selectedIndex = (int)i + 1;
		}
	}

	if (selectedPort.empty()) {
		selectedIndex = 0;
	} else if (selectedIndex == 0) {
		std::wstring missing = selectedPort;
		missing.append(L" (missing)");
		ComboBox_AddStringData(m_hWnd, IDC_EM_COMPORT, missing.c_str(), -2);
		m_missingPort = selectedPort;
		selectedIndex = (int)SendDlgItemMessageW(IDC_EM_COMPORT, CB_GETCOUNT, 0, 0) - 1;
	}

	SendDlgItemMessageW(IDC_EM_COMPORT, CB_SETCURSEL, selectedIndex, 0);
}

std::wstring CVREmitterPPage::GetSelectedPort() const
{
	const LONG_PTR data = ComboBox_GetCurItemData(m_hWnd, IDC_EM_COMPORT);
	if (data == -1 || data == CB_ERR) {
		return {};
	}
	if (data == -2) {
		return m_missingPort;
	}
	if (data >= 0 && (size_t)data < m_ports.size()) {
		return m_ports[data].port;
	}
	return {};
}

PageFlipConfig CVREmitterPPage::GetConfigFromControls() const
{
	PageFlipConfig cfg = m_cfg;

	const LONG_PTR drive = ComboBox_GetCurItemData(m_hWnd, IDC_EM_DRIVE_MODE);
	if (drive != CB_ERR) {
		cfg.irDriveMode = static_cast<int>(drive);
	}

	cfg.comPort = GetSelectedPort();

	cfg.irProtocol = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_PROTOCOL), cfg.irProtocol);
	cfg.irFrameDelay = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_FRAME_DELAY), cfg.irFrameDelay);
	cfg.irFrameDuration = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_FRAME_DURATION), cfg.irFrameDuration);
	cfg.irSignalSpacing = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_SIGNAL_SPACING), cfg.irSignalSpacing);
	cfg.irFlipEyes = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_FLIP_EYES), cfg.irFlipEyes);
	cfg.irAverageTimingMode = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_AVG_TIMING), cfg.irAverageTimingMode);
	cfg.targetFrametime = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_TARGET_FRAMETIME), cfg.targetFrametime);

	cfg.optBlockSignalDetectionDelay = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_BLOCK_DELAY), cfg.optBlockSignalDetectionDelay);
	cfg.optMinThresholdValueToActivate = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_MIN_THRESHOLD), cfg.optMinThresholdValueToActivate);
	cfg.optDetectionThresholdHigh = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_THRESH_HIGH), cfg.optDetectionThresholdHigh);
	cfg.optDetectionThresholdLow = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_THRESH_LOW), cfg.optDetectionThresholdLow);
	cfg.optEnableIgnoreDuringIr = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_IGNORE_DURING_IR), cfg.optEnableIgnoreDuringIr);
	cfg.optEnableDuplicateRealtimeReporting = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_DUP_REALTIME), cfg.optEnableDuplicateRealtimeReporting);
	cfg.optOutputStats = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_OUTPUT_STATS), cfg.optOutputStats);
	cfg.optIgnoreAllDuplicates = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_IGNORE_ALL), cfg.optIgnoreAllDuplicates);
	cfg.optSensorFilterMode = ParseIntValue(GetDlgItemTextString(m_hWnd, IDC_EM_SENSOR_FILTER), cfg.optSensorFilterMode);

	return cfg;
}

void CVREmitterPPage::UpdateEmitterState()
{
	if (!m_pFilterConfig) {
		return;
	}

	PageFlipEmitterStateBin* state = nullptr;
	unsigned size = 0;
	if (SUCCEEDED(m_pFilterConfig->Flt_GetBin("pageflip_emitter_state", (LPVOID*)&state, &size)) && state && size == sizeof(PageFlipEmitterStateBin)) {
		m_cfg = FromPageFlipConfigBin(state->config, m_cfg);
		m_connected = state->connected != 0;
		m_firmwareVersion = state->firmwareVersion;
		LocalFree(state);
	}
}

void CVREmitterPPage::UpdateLocalEmitterSettings()
{
	if (!m_pFilterConfig) {
		m_localSettings = LoadLocalEmitterSettings(m_configPath);
		return;
	}

	LocalEmitterSettingsBin* bin = nullptr;
	unsigned size = 0;
	if (SUCCEEDED(m_pFilterConfig->Flt_GetBin("pageflip_emitter_local_settings", (LPVOID*)&bin, &size))
			&& bin && size == sizeof(LocalEmitterSettingsBin)) {
		m_localSettings = FromLocalEmitterSettingsBin(*bin);
		LocalFree(bin);
		return;
	}

	m_localSettings = LoadLocalEmitterSettings(m_configPath);
}

void CVREmitterPPage::ApplyLocalEmitterSettings(bool disableAutoConnect, const std::wstring& comPort)
{
	m_localSettings.disableAutoConnect = disableAutoConnect;
	m_localSettings.comPort = comPort;

	if (!m_pFilterConfig) {
		return;
	}

	LocalEmitterSettingsBin bin = ToLocalEmitterSettingsBin(m_localSettings);
	m_pFilterConfig->Flt_SetBin("pageflip_emitter_local_settings", &bin, sizeof(bin));
}

HRESULT CVREmitterPPage::OnConnect(IUnknown* pUnknown)
{
	if (pUnknown == nullptr) {
		return E_POINTER;
	}

	m_pVideoRenderer = pUnknown;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}
	m_pFilterConfig = pUnknown;
	if (!m_pFilterConfig) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVREmitterPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pFilterConfig.Release();
	m_pVideoRenderer.Release();
	return S_OK;
}

HRESULT CVREmitterPPage::OnActivate()
{
	m_hWnd = m_hwnd;
	m_configPath = GetDefaultPageFlipConfigPath();
	m_cfg = LoadPageFlipConfig(m_configPath);

	SendDlgItemMessageW(IDC_EM_DRIVE_MODE, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_EM_DRIVE_MODE, L"0 (optical)", 0);
	ComboBox_AddStringData(m_hWnd, IDC_EM_DRIVE_MODE, L"1 (serial)", 1);

	UpdateEmitterState();
	if (m_connected && m_pFilterConfig) {
		m_pFilterConfig->Flt_SetBool("pageflip_emitter_refresh", true);
		UpdateEmitterState();
	}
	UpdateLocalEmitterSettings();
	SetControls();

	AddHint(IDC_EM_COMPORT, L"Used as the serial port when connecting to the emitter. On Windows use COMx.");
	AddHint(IDC_EM_REFRESH, L"Refresh the list of available COM ports.");
	AddHint(IDC_EM_CONNECT, L"Connect to the emitter on the selected serial port.");
	AddHint(IDC_EM_DISCONNECT, L"Disconnect from the emitter.");
	AddHint(IDC_EM_DISABLE_AUTOCONNECT, L"Disable automatic scanning and reconnection attempts.");
	AddHint(IDC_EM_FIRMWARE, L"The version of your IR emitter firmware.");
	AddHint(IDC_EM_DRIVE_MODE, L"0=Optical, 1=PCSerial. Calibration hotkey: T toggles drive mode. (Firmware 20+ for drive mode setting.)");
	AddHint(IDC_EM_PROTOCOL, L"0=Samsung07, 1=Xpand, 2=3DVision, 3=Sharp, 4=Sony, 5=Panasonic, 6=PanasonicCustom, 7=DLPLink.");
	AddHint(IDC_EM_FRAME_DELAY, L"(us) Delay after signal before activating glasses.");
	AddHint(IDC_EM_FRAME_DURATION, L"(us) Duration to keep glasses active after activation.");
	AddHint(IDC_EM_SIGNAL_SPACING, L"(us) Delay between IR signals to avoid overloading receiver.");
	AddHint(IDC_EM_FLIP_EYES, L"Set to 1 to flip left/right eye signals.");
	AddHint(IDC_EM_AVG_TIMING, L"0=Disable, 1=Mode 1 (averaging to reduce jitter).");
	AddHint(IDC_EM_TARGET_FRAMETIME, L"(us) Expected frame time (1,000,000 / refresh rate). Used for averaging.");
	AddHint(IDC_EM_BLOCK_DELAY, L"(us) Block detection after a trigger to avoid false repeats.");
	AddHint(IDC_EM_IGNORE_ALL, L"Ignore all duplicate detections (0=off, 1=on).");
	AddHint(IDC_EM_MIN_THRESHOLD, L"(1-255) Min intensity required before emitter turns on.");
	AddHint(IDC_EM_THRESH_HIGH, L"(1-255) Rising threshold to detect new frame start.");
	AddHint(IDC_EM_THRESH_LOW, L"(1-255) Falling threshold required before accepting next trigger.");
	AddHint(IDC_EM_IGNORE_DURING_IR, L"Disable opt detection during IR emission to avoid false triggers.");
	AddHint(IDC_EM_DUP_REALTIME, L"Emit realtime duplicate reports over serial (0=off, 1=on).");
	AddHint(IDC_EM_OUTPUT_STATS, L"Output opt module stats over serial (0=off, 1=on).");
	AddHint(IDC_EM_SENSOR_FILTER, L"Sensor filter mode (0=off, 1=mode 1).");
	AddHint(IDC_EM_READ, L"Read current settings from the emitter.");
	AddHint(IDC_EM_APPLY, L"Apply settings to the emitter.");
	AddHint(IDC_EM_SAVE_EEPROM, L"Save current emitter settings to EEPROM.");
	AddHint(IDC_EM_LOAD_JSON, L"Load emitter settings from JSON.");
	AddHint(IDC_EM_SAVE_JSON, L"Save emitter settings to JSON.");
	AddHint(IDC_EM_UPDATE_FIRMWARE, L"Open firmware update dialog. Disabled while playback is active or the emitter is connected.");

	return S_OK;
}

INT_PTR CVREmitterPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		const int nID = LOWORD(wParam);
		const int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			if (nID == IDC_EM_REFRESH) {
				const std::wstring current = GetSelectedPort();
				PopulatePorts(current);
				return (LRESULT)1;
			}
			if (nID == IDC_EM_CONNECT && m_pFilterConfig) {
				ApplyLocalEmitterSettings(false, GetSelectedPort());
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_connect", true);
				UpdateEmitterState();
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_DISCONNECT && m_pFilterConfig) {
				ApplyLocalEmitterSettings(true, GetSelectedPort());
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_connect", false);
				UpdateEmitterState();
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_DISABLE_AUTOCONNECT && m_pFilterConfig) {
				const bool disable = IsDlgButtonChecked(IDC_EM_DISABLE_AUTOCONNECT) == BST_CHECKED;
				ApplyLocalEmitterSettings(disable, GetSelectedPort());
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_READ && m_pFilterConfig) {
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_refresh", true);
				UpdateEmitterState();
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_APPLY && m_pFilterConfig) {
				PageFlipConfig cfg = GetConfigFromControls();
				PageFlipConfigBin bin = ToPageFlipConfigBin(cfg);
				m_pFilterConfig->Flt_SetBin("pageflip_emitter_apply", &bin, sizeof(bin));
				m_cfg = cfg;
				SetControls();
				return (LRESULT)1;
			}
			if (nID == IDC_EM_SAVE_EEPROM && m_pFilterConfig) {
				m_pFilterConfig->Flt_SetBool("pageflip_emitter_save", true);
				return (LRESULT)1;
			}
			if (nID == IDC_EM_LOAD_JSON) {
				std::wstring path;
				if (BrowseOpenFile(m_hWnd, L"Emitter Settings (*.emitter_settings.json)\0*.emitter_settings.json\0All Files\0*.*\0", L"emitter_settings.json", path)) {
					PageFlipConfig cfg = m_cfg;
					if (LoadEmitterSettingsJson(path, cfg)) {
						m_cfg = cfg;
						SetControls();
						SetDirty();
					}
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_SAVE_JSON) {
				std::wstring path;
				if (BrowseSaveFile(m_hWnd, L"Emitter Settings (*.emitter_settings.json)\0*.emitter_settings.json\0All Files\0*.*\0", L"emitter_settings.json", path)) {
					PageFlipConfig cfg = GetConfigFromControls();
					SaveEmitterSettingsJson(path, cfg);
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_UPDATE_FIRMWARE) {
				if (!m_pFilterConfig) {
					::MessageBoxW(m_hWnd, L"Unable to access renderer state.", L"Firmware Update", MB_ICONERROR | MB_OK);
					return (LRESULT)1;
				}

				UpdateEmitterState();
				UpdateLocalEmitterSettings();

				if (m_connected) {
					::MessageBoxW(m_hWnd, L"Disconnect from the emitter before updating firmware.", L"Firmware Update", MB_ICONWARNING | MB_OK);
					return (LRESULT)1;
				}

				int state = State_Stopped;
				if (FAILED(m_pFilterConfig->Flt_GetInt("playbackState", &state)) || state != State_Stopped) {
					::MessageBoxW(m_hWnd, L"Stop playback before updating firmware.", L"Firmware Update", MB_ICONWARNING | MB_OK);
					return (LRESULT)1;
				}

				const LocalEmitterSettings prevSettings = m_localSettings;
				if (!m_localSettings.disableAutoConnect) {
					ApplyLocalEmitterSettings(true, prevSettings.comPort);
					UpdateLocalEmitterSettings();
				}

				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_CONNECT), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_DISCONNECT), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_COMPORT), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_REFRESH), FALSE);
				::EnableWindow(::GetDlgItem(m_hWnd, IDC_EM_DISABLE_AUTOCONNECT), FALSE);

				EmitterFirmwareDialog dialog(m_hWnd);
				dialog.ShowModal();

				ApplyLocalEmitterSettings(prevSettings.disableAutoConnect, prevSettings.comPort);
				UpdateLocalEmitterSettings();
				SetControls();
				return (LRESULT)1;
			}
		}

		if (action == CBN_SELCHANGE) {
			if (nID == IDC_EM_COMPORT) {
				if (!m_loading) {
					ApplyLocalEmitterSettings(m_localSettings.disableAutoConnect, GetSelectedPort());
					UpdateLocalEmitterSettings();
					SetControls();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_EM_DRIVE_MODE) {
				if (!m_loading) {
					SetDirty();
				}
				return (LRESULT)1;
			}
		}

		if (action == EN_CHANGE) {
			switch (nID) {
			case IDC_EM_PROTOCOL:
			case IDC_EM_FRAME_DELAY:
			case IDC_EM_FRAME_DURATION:
			case IDC_EM_SIGNAL_SPACING:
			case IDC_EM_FLIP_EYES:
			case IDC_EM_AVG_TIMING:
			case IDC_EM_TARGET_FRAMETIME:
			case IDC_EM_BLOCK_DELAY:
			case IDC_EM_MIN_THRESHOLD:
			case IDC_EM_THRESH_HIGH:
			case IDC_EM_THRESH_LOW:
			case IDC_EM_IGNORE_DURING_IR:
			case IDC_EM_DUP_REALTIME:
			case IDC_EM_OUTPUT_STATS:
			case IDC_EM_IGNORE_ALL:
			case IDC_EM_SENSOR_FILTER:
				if (!m_loading) {
					SetDirty();
				}
				return (LRESULT)1;
			default:
				break;
			}
		}
	}

	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVREmitterPPage::OnApplyChanges()
{
	PageFlipConfig cfg = GetConfigFromControls();
	SavePageFlipConfig(m_configPath, cfg);
	m_cfg = cfg;
	if (m_pFilterConfig) {
		PageFlipConfigBin bin = ToPageFlipConfigBin(cfg);
		m_pFilterConfig->Flt_SetBin("pageflip_apply_config", &bin, sizeof(bin));
	}
	return S_OK;
}

// CVRInfoPPage

CVRInfoPPage::CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"InfoProp", lpunk, IDD_INFOPROPPAGE, IDS_INFOPROPPAGE_TITLE)
{
	DLog(L"CVRInfoPPage()");
}

CVRInfoPPage::~CVRInfoPPage()
{
	DLog(L"~CVRInfoPPage()");

	if (m_hMonoFont) {
		DeleteObject(m_hMonoFont);
		m_hMonoFont = 0;
	}
}

HRESULT CVRInfoPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRInfoPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HWND GetParentOwner(HWND hwnd)
{
	HWND hWndParent = hwnd;
	HWND hWndT;
	while ((::GetWindowLongPtrW(hWndParent, GWL_STYLE) & WS_CHILD) &&
		(hWndT = ::GetParent(hWndParent)) != NULL) {
		hWndParent = hWndT;
	}

	return hWndParent;
}

static WNDPROC OldControlProc;
static LRESULT CALLBACK ControlProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_KEYDOWN && LOWORD(wParam) == VK_ESCAPE) {
		// fixed Esc handling when EDITTEXT control has ES_MULTILINE property and is in focus
		HWND parentOwner = GetParentOwner(control);
		if (parentOwner) {
			::PostMessageW(parentOwner, WM_COMMAND, IDCANCEL, 0);
		}
		return TRUE;
	}

	return CallWindowProcW(OldControlProc, control, message, wParam, lParam); // call edit control's own windowproc
}

HRESULT CVRInfoPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	// init monospace font
	LOGFONTW lf = {};
	HDC hdc = GetWindowDC();
	lf.lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hdc);
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	wcscpy_s(lf.lfFaceName, L"Consolas");
	m_hMonoFont = CreateFontIndirectW(&lf);

	GetDlgItem(IDC_EDIT1).SetFont(m_hMonoFont);
	ASSERT(m_pVideoRenderer);

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
		return S_OK;
	}

	std::wstring strInfo(L"Windows ");
	strInfo.append(GetWindowsVersion());
	strInfo.append(L"\r\n");

	std::wstring strVP;
	if (S_OK == m_pVideoRenderer->GetVideoProcessorInfo(strVP)) {
		str_replace(strVP, L"\n", L"\r\n");
		strInfo.append(strVP);
	}

#ifdef _DEBUG
	{
		std::vector<DisplayConfig_t> displayConfigs;

		bool ret = GetDisplayConfigs(displayConfigs);

		strInfo.append(L"\r\n");

		for (const auto& dc : displayConfigs) {
			double freq = (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
			strInfo += std::format(L"\r\n{} - {:.3f} Hz", dc.displayName, freq);

			if (dc.bitsPerChannel) { // if bitsPerChannel is not set then colorEncoding and other values are invalid
				const wchar_t* colenc = ColorEncodingToString(dc.colorEncoding);
				if (colenc) {
					strInfo += std::format(L" {}", colenc);
				}
				strInfo += std::format(L" {}-bit", dc.bitsPerChannel);
			}

			const wchar_t* output = OutputTechnologyToString(dc.outputTechnology);
			if (output) {
				strInfo += std::format(L" {}", output);
			}
		}
	}
#endif

	SetDlgItemTextW(IDC_EDIT1, strInfo.c_str());

	OldControlProc = (WNDPROC)::SetWindowLongPtrW(::GetDlgItem(m_hWnd, IDC_EDIT1), GWLP_WNDPROC, (LONG_PTR)ControlProc);

	return S_OK;
}
