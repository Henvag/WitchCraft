#include "Link.h"
#include "Overlay.h"
#include "NativeAdapter.h"

#include <Windows.h>
#include <unknwn.h>

#include <cstring>
#include <cstdio>

namespace
{
	HMODULE g_real = nullptr;
	wchar_t g_logPath[MAX_PATH]{};

	void Log(const char* a_line)
	{
		if (!g_logPath[0]) {
			return;
		}
		HANDLE file = ::CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
		DWORD wrote = 0;
		::WriteFile(file, a_line, static_cast<DWORD>(std::strlen(a_line)), &wrote, nullptr);
		::WriteFile(file, "\r\n", 2, &wrote, nullptr);
		::CloseHandle(file);
	}

	HMODULE Real()
	{
		if (!g_real) {
			wchar_t path[MAX_PATH]{};
			::GetSystemDirectoryW(path, MAX_PATH);
			::lstrcatW(path, L"\\dinput8.dll");
			g_real = ::LoadLibraryW(path);
		}
		return g_real;
	}

	DWORD WINAPI HostThread(LPVOID)
	{
		wchar_t dir[MAX_PATH]{};
		if (::GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) {
			::lstrcatW(dir, L"\\WitchCraft");
			::CreateDirectoryW(dir, nullptr);
			::lstrcpyW(g_logPath, dir);
			::lstrcatW(g_logPath, L"\\host.log");
		}
		Log("WitchCraft host loaded");
		auto& link = witchcraft::Link::Get();
		if (!link.Create()) {
			Log("shared memory create failed");
			return 0;
		}
		Log("shared memory ready");
		witchcraft::StartNativeAdapter(Log);
        LARGE_INTEGER frequency{},clock{};
        ::QueryPerformanceFrequency(&frequency);::QueryPerformanceCounter(&clock);
        const auto period=frequency.QuadPart/60;
        auto deadline=clock.QuadPart,windowAt=clock.QuadPart;
        std::int64_t workTotal=0,workPeak=0;unsigned frames=0;
        HANDLE timer=::CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
		for (;;) {
            LARGE_INTEGER begin{},end{};::QueryPerformanceCounter(&begin);
			witchcraft::PollNativeAdapter();
			link.Heartbeat();
			witchcraft::PresentOverlay();
            ::QueryPerformanceCounter(&end);
            const auto work=end.QuadPart-begin.QuadPart;
            workTotal+=work;if(work>workPeak) workPeak=work;++frames;
            if(end.QuadPart-windowAt>=frequency.QuadPart*5) {
                char line[192]{};std::snprintf(line,sizeof(line),"host timing: fps=%.1f workAvgMs=%.2f workPeakMs=%.2f",frames*static_cast<double>(frequency.QuadPart)/(end.QuadPart-windowAt),workTotal*1000.0/frequency.QuadPart/frames,workPeak*1000.0/frequency.QuadPart);Log(line);
                windowAt=end.QuadPart;workTotal=workPeak=0;frames=0;
            }
            deadline+=period;
            if(end.QuadPart>=deadline) {deadline=end.QuadPart;continue;}
            const auto remaining=deadline-end.QuadPart;
            LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(remaining*10000000.0/frequency.QuadPart);
            if(timer&&::SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)) ::WaitForSingleObject(timer,50);
            else ::Sleep(static_cast<DWORD>(remaining*1000/frequency.QuadPart));
		}
	}

	void StartHost()
	{
		static volatile LONG started = 0;
		if (::InterlockedCompareExchange(&started, 1, 0) != 0) {
			return;
		}
		::CreateThread(nullptr, 0, HostThread, nullptr, 0, nullptr);
	}
}

extern "C"
{
	HRESULT WINAPI DirectInput8Create(HINSTANCE a_inst, DWORD a_version, REFIID a_iid, LPVOID* a_out, LPUNKNOWN a_outer)
	{
		StartHost();
		using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
		auto fn = reinterpret_cast<Fn>(::GetProcAddress(Real(), "DirectInput8Create"));
		return fn ? fn(a_inst, a_version, a_iid, a_out, a_outer) : E_FAIL;
	}

	HRESULT WINAPI DllCanUnloadNow()
	{
		using Fn = HRESULT(WINAPI*)();
		auto fn = reinterpret_cast<Fn>(::GetProcAddress(Real(), "DllCanUnloadNow"));
		return fn ? fn() : S_FALSE;
	}

	HRESULT WINAPI DllGetClassObject(REFCLSID a_clsid, REFIID a_iid, LPVOID* a_out)
	{
		using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
		auto fn = reinterpret_cast<Fn>(::GetProcAddress(Real(), "DllGetClassObject"));
		return fn ? fn(a_clsid, a_iid, a_out) : E_FAIL;
	}

	HRESULT WINAPI DllRegisterServer()
	{
		using Fn = HRESULT(WINAPI*)();
		auto fn = reinterpret_cast<Fn>(::GetProcAddress(Real(), "DllRegisterServer"));
		return fn ? fn() : E_FAIL;
	}

	HRESULT WINAPI DllUnregisterServer()
	{
		using Fn = HRESULT(WINAPI*)();
		auto fn = reinterpret_cast<Fn>(::GetProcAddress(Real(), "DllUnregisterServer"));
		return fn ? fn() : E_FAIL;
	}

	BOOL WINAPI DllMain(HINSTANCE, DWORD a_reason, LPVOID)
	{
		if (a_reason == DLL_PROCESS_ATTACH) {
			StartHost();
		}
		return TRUE;
	}
}
