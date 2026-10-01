// FluentSwitcher.json, the engine's settings file, as the settings window sees it: read whole, the
// fields it shows are changed in place, and it is written back whole, so fields this window does not
// know (or that a newer engine added) stay as they were. Written like the engine writes it
// (nlohmann::json, sorted keys, two spaces), to a .tmp next to it and moved over the old file.
#pragma once

#include <wx/string.h>

#include "json.hpp"

class Config
{
public:
    // False and *error when the file is there but cannot be read; a missing file is an empty config
    // (the engine writes its defaults on its first start).
    bool Load(const wxString& path, wxString* error);
    bool Save(wxString* error) const;
    const wxString& Path() const { return m_path; }

    bool GetBool(const char* key, bool def) const;
    void SetBool(const char* key, bool value);
    int GetInt(const char* key, int def) const;
    void SetInt(const char* key, int value);
    wxString GetString(const char* key, const wxString& def) const;
    void SetString(const char* key, const wxString& value);

    // "hotkeys": { "hk_RevertLastWord": "Break, Shift + F24", ... }
    wxString GetHotkeys(const char* action) const;
    void SetHotkeys(const char* action, const wxString& value);

    nlohmann::json& Json() { return m_json; }
    const nlohmann::json& Json() const { return m_json; }
    bool operator==(const Config& other) const { return m_json == other.m_json; }
    bool operator!=(const Config& other) const { return !(*this == other); }

private:
    wxString m_path;
    nlohmann::json m_json = nlohmann::json::object();
};

wxString FromUtf8(const std::string& s);
std::string ToUtf8(const wxString& s);
