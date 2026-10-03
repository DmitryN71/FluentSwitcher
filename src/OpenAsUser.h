// Открыть адрес, файл или папку так, как открыл бы их рабочий стол - без прав администратора. Движок (и
// окно настроек, которое он запускает) работает от администратора, когда включено "Работать в программах,
// запущенных от имени администратора"; браузер или Блокнот, запущенные прямо из него, тоже получили бы
// эти права. Поэтому просим Проводник: его рабочий стол запускает с правами пользователя (способ из блога
// Raymond Chen, "How can I launch an unelevated process from my elevated process"). Без прав
// администратора, или если Проводника нет (другая оболочка, перезапуск), - обычный ShellExecute.
// Без wxWidgets: нужен и движку, и окну настроек.
#pragma once

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shldisp.h>
#include <wrl/client.h>

#include <string>

namespace OpenAsUserDetails {

inline bool IsElevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	TOKEN_ELEVATION elevation{};
	DWORD size = 0;
	const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
		elevation.TokenIsElevated;
	CloseHandle(token);
	return elevated;
}

inline bool ThroughDesktop(const std::wstring& target) {
	using Microsoft::WRL::ComPtr;
	const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	bool ok = false;
	{
		ComPtr<IShellWindows> windows;
		ComPtr<IDispatch> desktop;
		ComPtr<IServiceProvider> provider;
		ComPtr<IShellBrowser> browser;
		ComPtr<IShellView> view;
		ComPtr<IDispatch> background;
		ComPtr<IShellFolderViewDual> folderView;
		ComPtr<IDispatch> application;
		ComPtr<IShellDispatch2> shell;
		VARIANT where;
		VariantInit(&where);
		where.vt = VT_I4;
		where.lVal = CSIDL_DESKTOP;
		VARIANT empty;
		VariantInit(&empty);
		long window = 0;
		if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows))) &&
			windows->FindWindowSW(&where, &empty, SWC_DESKTOP, &window, SWFO_NEEDDISPATCH, &desktop) == S_OK && desktop &&
			SUCCEEDED(desktop.As(&provider)) &&
			SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
			SUCCEEDED(browser->QueryActiveShellView(&view)) &&
			SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) &&
			SUCCEEDED(background.As(&folderView)) && SUCCEEDED(folderView->get_Application(&application)) &&
			application && SUCCEEDED(application.As(&shell))) {
			BSTR file = SysAllocString(target.c_str());
			VARIANT args, dir, verb, show;
			VariantInit(&args);
			VariantInit(&dir);
			VariantInit(&verb);
			verb.vt = VT_BSTR;
			verb.bstrVal = SysAllocString(L"open");
			VariantInit(&show);
			show.vt = VT_I4;
			show.lVal = SW_SHOWNORMAL;
			ok = file && SUCCEEDED(shell->ShellExecute(file, args, dir, verb, show));
			VariantClear(&verb);
			SysFreeString(file);
		}
	}
	if (SUCCEEDED(init))
		CoUninitialize();
	return ok;
}

}

inline bool OpenAsUser(const std::wstring& target) {
	if (OpenAsUserDetails::IsElevated() && OpenAsUserDetails::ThroughDesktop(target))
		return true;
	return (INT_PTR)ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}
