#pragma once

#include <windows.h>

// Своя учётная запись может получить права администратора: она в группе администраторов (токен разделён UAC, или UAC
// выключен и права уже есть). Под обычной учётной записью "от имени администратора" - это от имени другого пользователя:
// его пароль при каждом запуске, его задание в планировщике (срабатывает при его входе в Windows). Работать в
// приложениях администратора FluentSwitcher там не может (форум, WinnyS, 10.10.2026). И движку, и окну настроек.
inline bool CanElevateSelf() {
	static const bool can = [] {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return true;
		TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
		DWORD size = 0;
		const BOOL got = GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size);
		CloseHandle(token);
		if (got && type != TokenElevationTypeDefault) return true; // токен разделён UAC (или уже полный): администратор
		// Токен не разделён: UAC выключен или пользователь обычный - в группе ли администраторов.
		BYTE sid[SECURITY_MAX_SID_SIZE];
		DWORD sidSize = sizeof(sid);
		BOOL member = FALSE;
		if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize) ||
			!CheckTokenMembership(nullptr, sid, &member))
			return true; // не узнали - как раньше
		return member != FALSE;
	}();
	return can;
}
