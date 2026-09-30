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
    std::ifstream in(path.wc_str(), std::ios::binary);
    if (!in)
        return true; // no file yet: the engine's defaults
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
    const wxString tmp = m_path + ".tmp";
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
