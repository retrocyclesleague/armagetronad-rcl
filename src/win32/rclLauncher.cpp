/*

*************************************************************************

Armagetron Advanced -- Retrocycles RCL Windows launcher
Copyright (C) 2026 Retrocycles League

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

***************************************************************************

*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace
{
bool rcl_checkInstall = false;

std::wstring rcl_WindowsError(DWORD error)
{
    wchar_t *message = 0;
    DWORD const length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        0, error, 0, reinterpret_cast<wchar_t *>(&message), 0, 0);

    std::wstring result;
    if (length && message)
        result.assign(message, length);
    else
        result = L"Windows error " + std::to_wstring(error);
    if (message)
        LocalFree(message);
    return result;
}

int rcl_Error(wchar_t const *action, DWORD error)
{
    if (rcl_checkInstall)
        return static_cast<int>(error ? error : ERROR_GEN_FAILURE);

    std::wstring message(action);
    message += L"\n\n";
    message += rcl_WindowsError(error);
    MessageBoxW(0, message.c_str(), L"Retrocycles RCL",
                MB_OK | MB_ICONERROR);
    return static_cast<int>(error ? error : ERROR_GEN_FAILURE);
}

bool rcl_ModulePath(std::wstring &path, DWORD &error)
{
    DWORD size = 256;
    while (true)
    {
        std::vector<wchar_t> buffer(size);
        SetLastError(ERROR_SUCCESS);
        DWORD const length = GetModuleFileNameW(0, &buffer[0], size);
        if (!length)
        {
            error = GetLastError();
            return false;
        }
        if (length < size)
        {
            path.assign(&buffer[0], length);
            return true;
        }
        if (size == 32768)
            break;
        size = size > 16384 ? 32768 : size * 2;
    }

    error = ERROR_INSUFFICIENT_BUFFER;
    return false;
}

std::wstring rcl_QuoteArgument(std::wstring const &argument)
{
    std::wstring quoted(1, L'"');
    size_t backslashes = 0;
    for (wchar_t character: argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
        }
        else if (character == L'"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += character;
            backslashes = 0;
        }
        else
        {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted += character;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

bool rcl_IsDirectory(std::wstring const &path)
{
    DWORD const attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool rcl_IsFile(std::wstring const &path)
{
    DWORD const attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    int argumentCount = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
        return rcl_Error(L"Could not read the launcher command line.",
                         GetLastError());
    for (int i = 1; i < argumentCount; ++i)
    {
        if (wcscmp(arguments[i], L"--rcl-check-install") == 0)
        {
            rcl_checkInstall = true;
            break;
        }
    }

    DWORD error = ERROR_SUCCESS;
    std::wstring launcherPath;
    if (!rcl_ModulePath(launcherPath, error))
        return rcl_Error(L"Could not locate the RCL launcher.", error);

    std::wstring::size_type const separator =
        launcherPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return rcl_Error(L"Could not locate the RCL package directory.",
                         ERROR_BAD_PATHNAME);

    std::wstring root = launcherPath.substr(0, separator);
    // C:\file.exe and \\?\C:\file.exe otherwise produce drive-relative C:.
    if (!root.empty() && root[root.size() - 1] == L':')
        root += L'\\';
    std::wstring const client = root + L"\\bin\\armagetronad.exe";
    std::wstring const config = root + L"\\config";
    if (!rcl_IsFile(client) || !rcl_IsDirectory(config))
        return rcl_Error(L"The RCL package is incomplete. Extract the full "
                         L"folder before running it.", ERROR_FILE_NOT_FOUND);

    wchar_t appDataPath[MAX_PATH] = {};
    HRESULT const appDataResult = SHGetFolderPathW(
        0, CSIDL_APPDATA | CSIDL_FLAG_CREATE, 0, SHGFP_TYPE_CURRENT,
        appDataPath);
    if (FAILED(appDataResult))
        return rcl_Error(L"Could not locate your Windows profile.",
                         static_cast<DWORD>(appDataResult));
    std::wstring const appData(appDataPath);
    std::wstring const profile = appData + L"\\Retrocycles RCL Client";
    if (!rcl_IsDirectory(profile) &&
            !CreateDirectoryW(profile.c_str(), 0) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
        return rcl_Error(L"Could not create the RCL profile directory.",
                         GetLastError());
    if (!rcl_IsDirectory(profile))
        return rcl_Error(L"The RCL profile path is not a directory.",
                         ERROR_DIRECTORY);

    std::wstring commandLine = rcl_QuoteArgument(client);
    commandLine += L" --datadir " + rcl_QuoteArgument(root);
    commandLine += L" --configdir " + rcl_QuoteArgument(config);
    commandLine += L" --userdatadir " + rcl_QuoteArgument(profile);
    commandLine += L" --userconfigdir " + rcl_QuoteArgument(profile + L"\\config");
    for (int i = 1; i < argumentCount; ++i)
        commandLine += L" " + rcl_QuoteArgument(arguments[i]);
    LocalFree(arguments);

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(),
                                             commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(client.c_str(), &mutableCommandLine[0], 0, 0, FALSE,
                        0, 0, root.c_str(), &startup, &process))
        return rcl_Error(L"Could not start the RCL client.", GetLastError());

    CloseHandle(process.hThread);
    DWORD const waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0)
    {
        error = GetLastError();
        CloseHandle(process.hProcess);
        return rcl_Error(L"Could not wait for the RCL client.", error);
    }

    DWORD exitCode = ERROR_GEN_FAILURE;
    if (!GetExitCodeProcess(process.hProcess, &exitCode))
    {
        error = GetLastError();
        CloseHandle(process.hProcess);
        return rcl_Error(L"Could not read the RCL client result.", error);
    }
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
