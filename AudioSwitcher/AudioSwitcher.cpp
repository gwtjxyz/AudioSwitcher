#include "framework.h"
#include "AudioSwitcher.h"
#include "strsafe.h"
#include "mmdeviceapi.h"
#include "PropIdl.h"
#include "PolicyConfig.h"
#include <Functiondiscoverykeys_devpkey.h>

#include "shlwapi.h"
#pragma comment(lib, "shlwapi.lib")

#include <vector>

#define MAX_LOADSTRING 100

// Surely this won't overflow
#define MAX_AUDIO_DEVICE_NAME 256

#define NOTIFICATION_TRAY_ICON_MSG (WM_USER + 0x100)

#define MENU_ITEM_AUDIO_SETTINGS (IDM_EXIT + 1)
#define MENU_ITEM_AUDIO_DEVICE_START (MENU_ITEM_AUDIO_SETTINGS + 1)

// Global Variables:
HINSTANCE g_hInst;                                // current instance
WCHAR g_szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR g_szWindowClass[MAX_LOADSTRING];            // the main window class name
IMMDeviceEnumerator * g_pEnumerator;              // Multimedia Device Enumerator

struct AudioDeviceEntry {
    unsigned int tag;
    TCHAR * pDeviceId;
};

std::vector<AudioDeviceEntry> g_Entries;

void SetDefaultAudioDevice(TCHAR * deviceId) {
    IPolicyConfigVista * pPolicyConfig;

    HRESULT hr = CoCreateInstance(__uuidof(CPolicyConfigVistaClient),
                                  NULL, CLSCTX_ALL, __uuidof(IPolicyConfigVista), (LPVOID *) &pPolicyConfig);
    if (SUCCEEDED(hr)) {
        // set as both multimedia and comms device
        hr = pPolicyConfig->SetDefaultEndpoint(deviceId, ERole::eMultimedia);
        hr = pPolicyConfig->SetDefaultEndpoint(deviceId, ERole::eCommunications);
        pPolicyConfig->Release();
    }
}

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                OpenContextMenu(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void                ClearEntries();

LRESULT CALLBACK    AboutBoxProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Limit to only one instance of this application running using mutexes
    const wchar_t * mutexName = L"AudioSwitcher-VeryLongSureToNotBeConflictingName";
    HANDLE hMutex = CreateMutexW(0, 0, mutexName); 

    if (!hMutex) {
        OutputDebugString(L"CreateMutex failed - exiting\n");
        return 1;
    }
    // Check if mutex already exists
    DWORD dw = GetLastError();
    if (dw == ERROR_ALREADY_EXISTS) {
        OutputDebugString(L"An instance of this application is already running.\n");
        return 1;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr != S_OK) {
        // TODO better error handling
        OutputDebugString(L"CoInitializeEx failed - exiting\n");
        return 1;
    }

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, g_szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_AUDIOSWITCHER, g_szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&g_pEnumerator);
    if (hr != S_OK) {
        // TODO better error handling
        OutputDebugString(L"Couldn't create audio enumerator thingy\n");
        return 1;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_AUDIOSWITCHER));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CoUninitialize();

    // Don't forget to release the mutex
    ReleaseMutex(hMutex);

    return (int) msg.wParam;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AUDIOSWITCHER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_AUDIOSWITCHER);
    wcex.lpszClassName  = g_szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON1));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   g_hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(g_szWindowClass, g_szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   NOTIFYICONDATA nid = {};
   nid.cbSize = sizeof(NOTIFYICONDATA);
   nid.hWnd = hWnd;
   nid.uID = 0;
   nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
   nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
   nid.uVersion = NOTIFYICON_VERSION_4;
   nid.uCallbackMessage = NOTIFICATION_TRAY_ICON_MSG;

   HRESULT hr = StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), L"Audio Switcher");

   Shell_NotifyIcon(NIM_ADD, &nid);
   Shell_NotifyIcon(NIM_SETVERSION, &nid);

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case NOTIFICATION_TRAY_ICON_MSG:
        {
            OpenContextMenu(hWnd, message, wParam, lParam);
            break;
        }
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case MENU_ITEM_AUDIO_SETTINGS:
            {
                // Open advanced audio settings
                SHELLEXECUTEINFOW execInfo {};
                execInfo.cbSize = sizeof(execInfo);
                execInfo.hwnd = NULL;
                execInfo.lpVerb = L"open";
                execInfo.lpFile = L"control.exe";
                execInfo.lpParameters = L"mmsys.cpl";
                execInfo.nShow = SW_SHOWNORMAL;

                if (!ShellExecuteExW(&execInfo)) {
                    OutputDebugString(L"Couldn't open audio settings!\n");
                }
                break;
            }
            case IDM_ABOUT:
            {
                DialogBoxW(NULL, MAKEINTRESOURCEW(IDD_ABOUTBOX), hWnd, AboutBoxProc);
                break;
            }
            case IDM_EXIT:
            {
                NOTIFYICONDATA nid = {};
                nid.cbSize = sizeof(NOTIFYICONDATA);
                nid.hWnd = hWnd;
                nid.uID = 0;
                Shell_NotifyIcon(NIM_DELETE, &nid);
                DestroyWindow(hWnd);
                break;
            }
            default:
                break;
            }
            // parse additional menu items created at runtime
            for (auto & e : g_Entries) {
                if (e.tag == wmId) {
                    SetDefaultAudioDevice(e.pDeviceId);
                    break;
                }
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void OpenContextMenu(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WORD event = LOWORD(lParam);
    switch (event) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU: {
            ClearEntries();

            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"Exit");
            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_ABOUT, L"About AudioSwitcher");
            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);

            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, MENU_ITEM_AUDIO_SETTINGS, L"Open Sound Settings");
            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);

            // add possible audio devices to the list
            IMMDeviceCollection * pDevices;
            HRESULT hr = g_pEnumerator->EnumAudioEndpoints(EDataFlow::eRender, DEVICE_STATE_ACTIVE, &pDevices);
            if (hr != S_OK) {
                OutputDebugString(L"Couldn't enumerate audio endpoints\n");
                exit(1);
            }

            unsigned int deviceCount = 0;
            hr = pDevices->GetCount(&deviceCount);
            IMMDevice * pDefaultDevice;
            TCHAR * pDefaultDeviceId;
            hr = g_pEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &pDefaultDevice);
            hr = pDefaultDevice->GetId(&pDefaultDeviceId);

            for (unsigned int i = 0; i < deviceCount; ++i) {
                IMMDevice * pDevice;
                hr = pDevices->Item(i, &pDevice);

                AudioDeviceEntry entry = {};
                UINT entryId = MENU_ITEM_AUDIO_DEVICE_START + i;
                entry.tag = entryId;
                hr = pDevice->GetId(&entry.pDeviceId);
                g_Entries.push_back(entry);
                IPropertyStore * pProperties;
                hr = pDevice->OpenPropertyStore(STGM_READ, &pProperties);

                PROPVARIANT varName;
                PropVariantInit(&varName);

                hr = pProperties->GetValue(PKEY_Device_FriendlyName, &varName);
                if (varName.vt != VT_EMPTY) {
                    const size_t bufSize = MAX_AUDIO_DEVICE_NAME;
                    TCHAR audioDeviceNameBuffer[bufSize];
                    StringCchPrintf(audioDeviceNameBuffer, bufSize, TEXT("[%d] %s"), i, varName.pwszVal);
                    
                    bool isDefault = StrCmp(entry.pDeviceId, pDefaultDeviceId) == 0 ? true : false;
                    unsigned int flags = MF_BYPOSITION | MF_STRING;
                    if (isDefault) {
                        OutputDebugString(TEXT("Found default device! \n"));
                        flags |= MF_CHECKED;
                    }
                    InsertMenu(hMenu, i, flags, entryId, audioDeviceNameBuffer);
                }

                PropVariantClear(&varName);
                pDevice->Release();
                pProperties->Release();
            }
            
            SetForegroundWindow(hWnd);

            int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);

            PostMessage(hWnd, WM_NULL, 0, 0);

            CoTaskMemFree(pDefaultDeviceId);
            pDefaultDevice->Release();
            pDevices->Release();

            break;
        }
        default:
            break;
    }
}

void ClearEntries() {
    for (auto & e : g_Entries) {
        if (e.pDeviceId != NULL) {
            CoTaskMemFree(e.pDeviceId);
        }
        e.tag = 0;
    }
    g_Entries.clear();
}

LRESULT CALLBACK AboutBoxProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            if (wParam == IDOK) {
                EndDialog(hDialog, TRUE);
                return TRUE;
            }
        default:
            return DefWindowProc(hDialog, message, wParam, lParam);
    }

    return FALSE;
}
