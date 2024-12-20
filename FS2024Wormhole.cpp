//#define _CRTDBG_MAP_ALLOC
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS 

#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <set>
#include <cpprest/http_client.h>
#include <cpprest/http_listener.h>
#include <cpprest/filestream.h>
#include <cpprest/json.h>
#include <filesystem>

#pragma comment(lib, "Version.lib")

using namespace std;
using namespace std::chrono;
using namespace std::this_thread;
using namespace utility;
using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::experimental::listener;

struct Sprocess {
    int id;
    HWND hwnd;
    DWORD processID;
    HANDLE hProcess;
    uintptr_t baseAddress;
    std::wstring version;
    std::vector<uintptr_t> offsets;
    long size = 0;

    double altitude = 0;
    double longitude = 0;
    double latitude = 0;
    double heading = 0;
};

std::vector<Sprocess*> processes;

struct Location {
    std::string name;
    double latitude;
    double longitude;
    double altitude;
    double heading;
};

std::vector<Location> locations;
const std::wstring locationsFilePath = L"locations.csv";
std::wstring iniFilePath;

std::string parseQuotedField(std::stringstream& ss) {
    std::string result;
    char c;
    bool inQuotes = false;

    while (ss.get(c)) {
        if (c == '"') {
            inQuotes = !inQuotes;
        }
        else if (c == ',' && !inQuotes) {
            break;
        }
        else {
            result += c;
        }
    }

    return result;
}

void LoadLocations() {
    std::ifstream file(locationsFilePath);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open file." << std::endl;
        return;
    }

    locations.clear();
    std::string line;

    // Skip header line
    if (!std::getline(file, line)) {
        file.close();
        return;
    }

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        Location loc;

        // Parse fields, respecting quoted strings
        loc.name = parseQuotedField(ss);

        std::string field;

        field = parseQuotedField(ss);
        loc.latitude = std::stod(field);

        field = parseQuotedField(ss);
        loc.longitude = std::stod(field);

        field = parseQuotedField(ss);
        loc.altitude = std::stod(field);

        field = parseQuotedField(ss);
        loc.heading = std::stod(field);

        locations.push_back(loc);
    }

    file.close();
}


void SaveLocations() {
    std::ofstream file(locationsFilePath);
    if (!file.is_open()) return;
    file << std::fixed << std::setprecision(8);
    file << "Name,Latitude,Longitude,Altitude,Heading\n";
    for (const auto& loc : locations) {
        file  << "\"" << loc.name << "\","
            << loc.latitude << ","
            << loc.longitude << ","
            << loc.altitude << ","
            << loc.heading << "\n";
    }
    file.close();
}

#define M_PI 3.14159265358979323846
double radiansToDegrees(double radians) {
    return radians * (180.0 / M_PI);
}

double degreesToRadians(double degrees) {
    return degrees * (M_PI / 180.0);
}

std::set<HWND> knownWindows;
std::vector<std::thread> threads;

uintptr_t GetModuleBaseAddress(DWORD processId) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32 moduleEntry;
    moduleEntry.dwSize = sizeof(MODULEENTRY32);

    if (Module32First(hSnapshot, &moduleEntry)) {
        do {
            if (_wcsicmp(moduleEntry.szModule, L"FlightSimulator2024.exe") == 0) {
                CloseHandle(hSnapshot);
                return (uintptr_t)moduleEntry.modBaseAddr;
            }
        } while (Module32Next(hSnapshot, &moduleEntry));
    }

    CloseHandle(hSnapshot);
    return 0;
}


HMODULE GetModule(HANDLE hProcess)
{
    HMODULE hMods[1024];
    HANDLE pHandle = hProcess;
    DWORD cbNeeded;
    unsigned int i;

    if (EnumProcessModules(pHandle, hMods, sizeof(hMods), &cbNeeded))
    {
        for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            TCHAR szModName[MAX_PATH];
            if (GetModuleFileNameEx(pHandle, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
            {
                wstring wstrModName = szModName;
                wstring wstrModContain = L"FlightSimulator2024.exe";
                if (wstrModName.find(wstrModContain) != string::npos)
                {
                    CloseHandle(pHandle);
                    return hMods[i];
                }
            }
        }
    }
    return nullptr;
}

std::wstring trim(const std::wstring& str) {
    size_t start = str.find_first_not_of(L" \t");
    size_t end = str.find_last_not_of(L" \t");
    if (start == std::wstring::npos || end == std::wstring::npos) {
        return L""; // Empty string if only spaces
    }
    return str.substr(start, end - start + 1);
}

// Function to extract text after the last "-" and trim
std::wstring extractAfterLastDash(const std::wstring& input) {
    size_t pos = input.find_last_of(L'-'); // Find the last occurrence of '-'
    if (pos == std::wstring::npos) {
        return trim(input); // No '-' found, return the trimmed string
    }
    return trim(input.substr(pos + 1)); // Extract and trim the part after '-'
}

void getProcessInfo(Sprocess* data) {
    DWORD processID;
    std::wstring version = L"0";
    GetWindowThreadProcessId(data->hwnd, &processID);
    data->processID = processID;

    wchar_t windowTitle[256];
    int length = GetWindowTextW(data->hwnd, windowTitle, sizeof(windowTitle) / sizeof(wchar_t));
    data->version = extractAfterLastDash(windowTitle);
}

std::vector<uintptr_t> ReadPointersFromConfig(Sprocess* data) {
    WCHAR buffer[4096] = { 0 };
    std::vector<uintptr_t> pointers;

    std::wstring sizeStr = std::to_wstring(data->size);
    std::wstring combinedKey = data->version + L"-" + sizeStr;
    DWORD charsRead = GetPrivateProfileString(L"Pointers", combinedKey.c_str(), L"", buffer, sizeof(buffer), iniFilePath.c_str());
    if (charsRead == 0) MessageBoxA(NULL, "Game version not supported", "Error", MB_OK);

    std::wstring rawString(buffer);
    std::wstringstream ss(rawString);
    std::wstring token;

    while (std::getline(ss, token, L',')) {
        uintptr_t value = std::stoull(token, nullptr, 0); // Convert token to uintptr_t
        pointers.push_back(value);
    }

    return pointers;
}

uintptr_t getCoordinatesPointer(Sprocess* data) {
    uintptr_t pointerAddress = data->baseAddress;
    uintptr_t valueAddress;
    for (size_t i = 1; i < data->offsets.size(); ++i) {
        if (!ReadProcessMemory(data->hProcess, (LPCVOID)pointerAddress, &valueAddress, sizeof(valueAddress), nullptr)) {
            pointerAddress = 0;
            break;
        }
        pointerAddress = valueAddress + data->offsets[i];
    }
    return pointerAddress;
}

void SetCords(Sprocess* data, double latitude, double longitude, double altitude, double heading) {
    latitude = degreesToRadians(latitude);
    longitude = degreesToRadians(longitude);
    if (heading >= 180) heading -= 360;
    heading = degreesToRadians(heading);

    uintptr_t pointerAddress = getCoordinatesPointer(data);
    double finalValue;
    if (pointerAddress > 0) {
        double newValue;
        WriteProcessMemory(data->hProcess, (LPVOID)(pointerAddress - 0x18), &heading, sizeof(heading), nullptr);
        WriteProcessMemory(data->hProcess, (LPVOID)pointerAddress, &latitude, sizeof(latitude), nullptr);
        WriteProcessMemory(data->hProcess, (LPVOID)(pointerAddress + 0x18), &longitude, sizeof(longitude), nullptr);
        WriteProcessMemory(data->hProcess, (LPVOID)(pointerAddress + 0x30), &altitude, sizeof(altitude), nullptr);
    }
}

void ProcessWindow(HWND hwnd, int id) {
    struct Sprocess* data = new Sprocess;
   
    data->id = id;
    data->hwnd = hwnd;
    getProcessInfo(data);
    data->hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, data->processID);
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameExW(data->hProcess, NULL, exePath, MAX_PATH);
    data->size = std::filesystem::file_size(exePath);
    data->offsets = ReadPointersFromConfig(data);
    processes.push_back(data);

    uintptr_t moduleBase = GetModuleBaseAddress(data->processID);
    data->baseAddress = moduleBase + data->offsets[0];

    while (IsWindow(hwnd)) {
        double finalValue;
        uintptr_t pointerAddress = getCoordinatesPointer(data);
        if (pointerAddress > 0) {
            if (ReadProcessMemory(data->hProcess, (LPCVOID)pointerAddress, &finalValue, sizeof(finalValue), nullptr)) {
                data->latitude = radiansToDegrees(finalValue);
            }
            if (ReadProcessMemory(data->hProcess, (LPCVOID) (pointerAddress + 0x18), &finalValue, sizeof(finalValue), nullptr)) {
                data->longitude = radiansToDegrees(finalValue);
            }
            if (ReadProcessMemory(data->hProcess, (LPCVOID) (pointerAddress + 0x30), &finalValue, sizeof(finalValue), nullptr)) {
                data->altitude = finalValue;
            }
            if (ReadProcessMemory(data->hProcess, (LPCVOID)(pointerAddress - 0x18), &finalValue, sizeof(finalValue), nullptr)) {
                finalValue = radiansToDegrees(finalValue);
                if (finalValue < 0) finalValue += 360;
                data->heading = finalValue;
            }
        }
        sleep_for(milliseconds(100));
    }
    processes.erase(std::remove(processes.begin(), processes.end(), data), processes.end());
    
    delete data;
}

const wchar_t* targetClassName = L"AceApp";
const wchar_t* targetWindowTitlePrefix = L"Microsoft Flight Simulator 2024";
std::mutex globalMutex;

int nextId = 1;
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    wchar_t title[2048];
    wchar_t className[2048];

    GetWindowText(hwnd, title, sizeof(title) / sizeof(wchar_t));
    GetClassName(hwnd, className, sizeof(className) / sizeof(wchar_t));

    if (wcsncmp(title, targetWindowTitlePrefix, wcslen(targetWindowTitlePrefix)) == 0 && wcsstr(className, targetClassName)) {
        globalMutex.lock();
        if (knownWindows.find(hwnd) == knownWindows.end()) { // if window is not in knownWindows set
            knownWindows.insert(hwnd);
            threads.push_back(std::thread(ProcessWindow, hwnd, nextId++));
        }
        globalMutex.unlock();
    }
    return TRUE;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_USER + 1:
        switch (lParam) {
        case WM_LBUTTONUP:
            ExitProcess(0);
            break;
        }
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

const wchar_t g_szClassName[] = L"MyTrayAppClass";

int GetNumberOfThreadsForCurrentProcess() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return -1; // Error
    }

    THREADENTRY32 threadEntry;
    threadEntry.dwSize = sizeof(THREADENTRY32);

    int threadCount = 0;
    DWORD currentProcessId = GetCurrentProcessId();

    if (Thread32First(hSnapshot, &threadEntry)) {
        do {
            if (threadEntry.th32OwnerProcessID == currentProcessId) {
                threadCount++;
            }
        } while (Thread32Next(hSnapshot, &threadEntry));
    }

    CloseHandle(hSnapshot);
    return threadCount;
}
const int ICON_WIDTH = 16;
const int ICON_HEIGHT = 16;

HICON icon = NULL;
HICON CreateNumberIcon(int number) {
    if (icon != NULL) DestroyIcon(icon);

    HDC hDC = CreateCompatibleDC(NULL);
    BITMAPINFO bmi = { 0 };
    HBITMAP hBitmap;
    HICON hIcon = NULL;

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ICON_WIDTH;
    bmi.bmiHeader.biHeight = -ICON_HEIGHT;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hBitmap = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);

    if (hBitmap) {
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hDC, hBitmap);
        RECT rect = { 0, 0, ICON_WIDTH, ICON_HEIGHT };

        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, RGB(255, 0, 255));  // white color
        SelectObject(hDC, hFont);
        DrawText(hDC, std::to_wstring(number).c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hDC, hOldBitmap);

        // Convert the bitmap to an icon
        ICONINFO ii = { 0 };
        ii.fIcon = TRUE;
        ii.hbmMask = hBitmap;
        ii.hbmColor = hBitmap;
        hIcon = CreateIconIndirect(&ii);

        DeleteObject(hFont);
        DeleteObject(hBitmap);
    }

    DeleteDC(hDC);
    icon = hIcon;
    return hIcon;
}
void SetupTrayIcon(HWND hwnd) {
    NOTIFYICONDATA nid = {};
    nid.uVersion = NOTIFYICON_VERSION_4;
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1; // ID for your icon for identification
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1; // Custom message ID for tray interactions
    nid.hIcon = CreateNumberIcon(0);
    wcscpy_s(nid.szTip, L"Thread count: 0"); // Initial tooltip

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void UpdateTooltip(HWND hwnd, int threadCount) {
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1; // Custom message ID for tray interactions
    nid.hIcon = CreateNumberIcon(threadCount);
    wsprintf(nid.szTip, L"Thread count: %d", threadCount);

    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

utility::string_t determine_content_type(const std::wstring& filePath)
{
    std::wstring extension = filePath.substr(filePath.find_last_of(L".") + 1);
    if (extension == L"html" || extension == L"htm") return U("text/html");
    if (extension == L"css") return U("text/css");
    if (extension == L"js") return U("application/javascript");
    if (extension == L"png") return U("image/png");
    if (extension == L"jpg" || extension == L"jpeg") return U("image/jpeg");
    if (extension == L"gif") return U("image/gif");
    if (extension == L"svg") return U("image/svg+xml");
    return U("application/octet-stream"); // Default to binary data
}

std::wstring conentPath;

std::wstring GetFullPath(const std::wstring& basePath, const std::wstring& fileName) {
    std::wstring fullPath = basePath;
    if (fullPath.back() != L'\\') {
        fullPath += L'\\';
    }
    fullPath += fileName;
    return fullPath;
}

void handle_request(http_request request)
{
    if (request.relative_uri().path() == U("/api/info") && request.method() == methods::GET)
    {
        json::value arr = json::value::array();
        for (const auto& process : processes) {
            json::value j;
            j[U("id")] = json::value::number((int)process->id);
            j[U("hwnd")] = json::value::number((int)process->hwnd);

            j[U("altitude")] = json::value::number((double)process->altitude);
            j[U("longitude")] = json::value::number((double)process->longitude);
            j[U("latitude")] = json::value::number((double)process->latitude);
            j[U("heading")] = json::value::number((double)process->heading);
            
            arr[arr.size()] = j;
        }

        request.reply(status_codes::OK, arr);
        return;
    }
    else if (request.method() == methods::GET)
    {
        std::wstring requestedPath = utility::conversions::to_string_t(request.relative_uri().path());
        if (requestedPath == L"/" || requestedPath.empty()) {
            requestedPath = L"/index.html";
        }
        std::wstring filePath = GetFullPath(conentPath, requestedPath);

        std::ifstream file(filePath);
        if (file.good()) {
            file.close();
            utility::string_t contentType = determine_content_type(filePath);
            concurrency::streams::fstream::open_istream(filePath, std::ios::binary).then(
                [request, contentType](concurrency::streams::istream fileStream)
                {
                    http_response response(status_codes::OK);
                    response.headers().set_content_type(contentType);
                    response.set_body(fileStream);
                    request.reply(response);
                })
                .wait();
                return;
        }
    }
    if (request.relative_uri().path() == U("/api/locations") && request.method() == methods::GET)
    {
        int id = 0;
        json::value arr = json::value::array();
        for (const auto& loc : locations) {
            json::value j;
            j[U("id")] = json::value::number(id++);
            j[U("name")] = json::value::string(utility::conversions::to_string_t(loc.name));
            j[U("latitude")] = json::value::number(loc.latitude);
            j[U("longitude")] = json::value::number(loc.longitude);
            j[U("altitude")] = json::value::number(loc.altitude);
            j[U("heading")] = json::value::number(loc.heading);

            arr[arr.size()] = j;
        }

        request.reply(status_codes::OK, arr);
        return;
    }
    else if (request.relative_uri().path() == U("/api/locations") && request.method() == methods::POST)
    {
        request.extract_json().then([request](json::value body) {
            Location newLoc;
            newLoc.name = utility::conversions::to_utf8string(body[U("name")].as_string());
            newLoc.latitude = body[U("latitude")].as_double();
            newLoc.longitude = body[U("longitude")].as_double();
            newLoc.altitude = body[U("altitude")].as_double();
            newLoc.heading = body[U("heading")].as_double();

            locations.push_back(newLoc);
            SaveLocations();

            request.reply(status_codes::OK);
            }).wait();
        return;
    }
    else if (request.relative_uri().path() == U("/api/locations") && request.method() == methods::DEL)
    {
        request.extract_json().then([request](json::value body) {
            std::string nameToDelete = utility::conversions::to_utf8string(body[U("name")].as_string());

            auto it = std::remove_if(locations.begin(), locations.end(),
                [&nameToDelete](const Location& loc) { return loc.name == nameToDelete; });

            locations.erase(it, locations.end());
            SaveLocations();

            request.reply(status_codes::OK);
            }).wait();
        return;
    }
    else if (request.relative_uri().path() == U("/api/setCords") && request.method() == methods::POST)
    {
        request.extract_json().then([request](json::value body) {
            double latitude = body[U("latitude")].as_double();
            double longitude = body[U("longitude")].as_double();
            double altitude = body[U("altitude")].as_double();
            double heading = body[U("heading")].as_double();

            if (!processes.empty()) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, latitude, longitude, altitude, heading);
                request.reply(status_codes::OK);
            }
            else {
                request.reply(status_codes::BadRequest, U("No active instances"));
            }
            }).wait();
        return;
    }
    request.reply(status_codes::BadRequest, U("Invalid request"));
}

void MainUpdateLoop(HWND hwnd) {
    while (true) {
        EnumWindows(EnumWindowsProc, NULL); 
        std::this_thread::sleep_for(std::chrono::seconds(1));
        UpdateTooltip(hwnd, processes.size());
    }
}

bool FileExists(const std::wstring& path) {
    DWORD fileAttr = GetFileAttributesW(path.c_str());
    return (fileAttr != INVALID_FILE_ATTRIBUTES &&
        !(fileAttr & FILE_ATTRIBUTE_DIRECTORY));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    std::locale::global(std::locale("en_US.UTF-8"));
    srand(time(NULL));
    const DWORD BUFFER_SIZE = 4096;
    WCHAR buffer[BUFFER_SIZE];
    WCHAR currentDir[BUFFER_SIZE];
    GetCurrentDirectoryW(BUFFER_SIZE, currentDir);

    iniFilePath = GetFullPath(currentDir, L"config.ini");
    if (!FileExists(iniFilePath)) {
        iniFilePath = L"D:\\Projects\\FS2024Wormhole\\config.ini";
    }
    DWORD charsRead = GetPrivateProfileString(L"Content", L"dir", L"", buffer, BUFFER_SIZE, iniFilePath.c_str());
    if (charsRead > 0) conentPath = buffer; else MessageBoxA(NULL, "can't parse config.ini", "Error", MB_OK);
    LoadLocations();

    WNDCLASSEX wc = {};
    HWND hwnd;
    MSG Msg;

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_szClassName;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    hwnd = CreateWindowEx(WS_EX_CLIENTEDGE, g_szClassName, L"The title of my window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120, NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        MessageBox(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    SetupTrayIcon(hwnd);

    http_listener listener(U("http://*:8089"));

    listener.support(methods::POST, handle_request);
    listener.support(methods::DEL, handle_request);
    listener.support(methods::GET, handle_request);

    try
    {
        listener
            .open()
            .wait();
    }
    catch (const web::http::http_exception& e)
    {
        std::wstring message = L"HTTP Exception: ";
        std::wstring whatMessage = utility::conversions::to_string_t(e.what()); 
        message += whatMessage;
        MessageBox(nullptr, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
    catch (const std::exception& e)
    {
        std::wstring message = L"Exception: ";
        std::wstring whatMessage = utility::conversions::to_string_t(e.what());
        message += whatMessage;
        MessageBox(nullptr, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }

    std::thread worker(MainUpdateLoop, hwnd);

    RegisterHotKey(NULL, 100, MOD_CONTROL, VK_ADD);
    RegisterHotKey(NULL, 101, MOD_CONTROL, VK_SUBTRACT);
    RegisterHotKey(NULL, 102, MOD_CONTROL | MOD_ALT, VK_ADD);
    RegisterHotKey(NULL, 103, MOD_CONTROL | MOD_ALT, VK_SUBTRACT);

    RegisterHotKey(NULL, 110, MOD_CONTROL, VK_LEFT);
    RegisterHotKey(NULL, 111, MOD_CONTROL, VK_RIGHT);
    RegisterHotKey(NULL, 112, MOD_CONTROL, VK_UP);
    RegisterHotKey(NULL, 113, MOD_CONTROL, VK_DOWN);

    RegisterHotKey(NULL, 120, MOD_CONTROL | MOD_ALT, VK_LEFT);
    RegisterHotKey(NULL, 121, MOD_CONTROL | MOD_ALT, VK_RIGHT);
    RegisterHotKey(NULL, 122, MOD_CONTROL | MOD_ALT, VK_UP);
    RegisterHotKey(NULL, 123, MOD_CONTROL | MOD_ALT, VK_DOWN);
    while (GetMessage(&Msg, NULL, 0, 0)) {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
        if (Msg.message == WM_QUIT) {
            return Msg.wParam;
        }
        else if (Msg.message == WM_HOTKEY) {
            if (Msg.wParam == 100) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude, firstProcess->altitude+1, firstProcess->heading);
            }
            if (Msg.wParam == 101) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude, firstProcess->altitude-1, firstProcess->heading);
            }
            if (Msg.wParam == 102) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude, firstProcess->altitude + 500, firstProcess->heading);
            }
            if (Msg.wParam == 103) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude, firstProcess->altitude - 500, firstProcess->heading);
            }
            if (Msg.wParam == 110) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude - 0.00001369, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 111) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude + 0.00001369, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 112) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude + 0.00001369, firstProcess->longitude, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 113) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude - 0.00001369, firstProcess->longitude, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 120) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude - 0.001369, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 121) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude, firstProcess->longitude + 0.001369, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 122) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude + 0.001369, firstProcess->longitude, firstProcess->altitude, firstProcess->heading);
            }
            if (Msg.wParam == 123) {
                Sprocess* firstProcess = processes[0];
                SetCords(firstProcess, firstProcess->latitude - 0.001369, firstProcess->longitude, firstProcess->altitude, firstProcess->heading);
            }
        }
    }

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);

    UnregisterHotKey(NULL, 100);

    return 0;
}