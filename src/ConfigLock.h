// FluentSwitcher.json пишут двое: движок (выученные слова, счёт исправлений раз в минуту) и окно настроек (Применить).
// Запись - по очереди, а прочитать-дополнить-записать - целиком под замком: иначе запись одного могла лечь поверх
// только что сделанной записи другого, и изменения пропадали. Замок не получен за 3 с (или его не открыть: движок
// от администратора, окно без прав) - пишем без него, как раньше.
#pragma once

#include <windows.h>

struct ConfigLock {
	HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\FluentSwitcher.Config");
	bool owned = false;
	ConfigLock() {
		if (mutex) {
			const DWORD r = WaitForSingleObject(mutex, 3000);
			owned = r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
		}
	}
	~ConfigLock() {
		if (owned) ReleaseMutex(mutex);
		if (mutex) CloseHandle(mutex);
	}
	ConfigLock(const ConfigLock&) = delete;
	ConfigLock& operator=(const ConfigLock&) = delete;
};
