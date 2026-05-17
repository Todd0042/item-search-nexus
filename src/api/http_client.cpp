#include "http_client.h"
#include "addon.h"
#include <winhttp.h>
#include <sstream>
#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

HttpClient& HttpClient::Instance()
{
    static HttpClient instance;
    return instance;
}

HttpClient::HttpClient() {}
HttpClient::~HttpClient() {}

HttpResult HttpClient::Get(const std::string& url, const std::string& authorization)
{
    HttpResult result;

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);

    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[4096] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 4096;

    std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &urlComp))
    {
        LogWarn("WinHttpCrackUrl failed");
        return result;
    }

    HINTERNET hSession = WinHttpOpen(L"Gw2ItemSearch/0.1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession)
    {
        LogWarn("WinHttpOpen failed");
        return result;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (urlComp.nScheme == INTERNET_SCHEME_HTTPS)
        flags |= WINHTTP_FLAG_SECURE;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL, NULL, NULL, flags);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring headers;
    if (!authorization.empty())
    {
        std::wstring wauth(authorization.begin(), authorization.end());
        headers = L"Authorization: Bearer " + wauth + L"\r\n";
    }
    headers += L"Accept: application/json";

    if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), NULL, 0, 0, 0))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &statusCodeSize, NULL);
    result.StatusCode = statusCode;

    std::string body;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
    {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
        {
            body.append(buffer.data(), bytesRead);
        }
    }

    result.Body = body;
    result.Success = (statusCode >= 200 && statusCode < 300);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

HttpResult HttpClient::GetJson(const std::string& endpoint, const std::string& apiKey)
{
    std::string url = m_baseUrl + endpoint;
    return Get(url, apiKey);
}

bool HttpClient::DownloadFile(const std::string& url, const std::string& filePath)
{
    HttpResult result = Get(url);
    if (!result.Success || result.Body.empty())
        return false;

    std::ofstream file(filePath, std::ios::binary);
    if (!file)
        return false;

    file.write(result.Body.data(), result.Body.size());
    return true;
}
