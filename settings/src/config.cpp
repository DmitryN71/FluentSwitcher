#include "config.h"

#include <wx/msw/wrapwin.h>

#include <fstream>
#include <iomanip>
#include <sstream>

wxString FromUtf8(const std::string& s)
{
    return wxString::FromUTF8(s.data(), s.size());
}

std::string ToUtf8(const wxString& s)
{
    const wxScopedCharBuffer utf8 = s.utf8_str();
    return std::string(utf8.data(), utf8.length());
}

bool Config::Load(const wxString& path, wxString* error)
{
    m_path = path;
    m_json = nlohmann::json::object();
    if (GetFileAttributesW(path.wc_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        return true; // no file yet: the engine's defaults
    // The file is there but may be held a moment (an antivirus, a sync client, the engine replacing it): a few tries,
    // then an error - not an empty config, which "Сохранить" would write over all the settings (review 11.10.2026).
    std::ifstream in;
    for (int attempt = 0; attempt < 10; attempt++)
    {
        in.open(path.wc_str(), std::ios::binary);
        if (in)
            break;
        in.clear();
        Sleep(50);
    }
    if (!in)
    {
        *error = wxString::Format("can't open %s (error %lu)", path, GetLastError());
        return false;
    }
    try
    {
        // Comments allowed, as the engine reads it.
        m_json = nlohmann::json::parse(in, nullptr, true, true);
        if (!m_json.is_object())
            throw std::runtime_error("not an object");
    }
    catch (const std::exception& e)
    {
        *error = FromUtf8(e.what());
        m_json = nlohmann::json::object();
        return false;
    }
    return true;
}

bool Config::Save(wxString* error) const
{
    // Its own temporary file (the engine's is .engine.tmp): a shared one, written by both at once, mixed two writes into
    // one broken file.
    const wxString tmp = m_path + ".settings.tmp";
    {
        std::ofstream out(tmp.wc_str(), std::ios::binary | std::ios::trunc);
        out << std::setw(2) << m_json << std::endl;
        out.close();
        if (!out)
        {
            *error = "can't write " + tmp;
            return false;
        }
    }
    // The data on the disk before the rename: MOVEFILE_WRITE_THROUGH makes only the rename itself durable, and a power
    // loss right after it left a file of zeros in place of the settings (review 11.10.2026).
    HANDLE written = CreateFileW(tmp.wc_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (written != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(written);
        CloseHandle(written);
    }
    if (!MoveFileExW(tmp.wc_str(), m_path.wc_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        *error = wxString::Format("can't replace %s (error %lu)", m_path, GetLastError());
        DeleteFileW(tmp.wc_str());
        return false;
    }
    return true;
}

bool Config::GetBool(const char* key, bool def) const
{
    auto it = m_json.find(key);
    return it != m_json.end() && it->is_boolean() ? it->get<bool>() : def;
}

void Config::SetBool(const char* key, bool value)
{
    m_json[key] = value;
}

int Config::GetInt(const char* key, int def) const
{
    auto it = m_json.find(key);
    return it != m_json.end() && it->is_number() ? it->get<int>() : def;
}

void Config::SetInt(const char* key, int value)
{
    m_json[key] = value;
}

wxString Config::GetString(const char* key, const wxString& def) const
{
    auto it = m_json.find(key);
    return it != m_json.end() && it->is_string() ? FromUtf8(it->get<std::string>()) : def;
}

void Config::SetString(const char* key, const wxString& value)
{
    m_json[key] = ToUtf8(value);
}

wxString Config::GetHotkeys(const char* action) const
{
    auto hk = m_json.find("hotkeys");
    if (hk == m_json.end() || !hk->is_object())
        return wxString();
    auto it = hk->find(action);
    return it != hk->end() && it->is_string() ? FromUtf8(it->get<std::string>()) : wxString();
}

void Config::SetHotkeys(const char* action, const wxString& value)
{
    m_json["hotkeys"][action] = ToUtf8(value);
}
