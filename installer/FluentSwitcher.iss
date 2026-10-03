; The FluentSwitcher installer (Inno Setup 6). Built by tools\make_release.ps1, which passes:
;   AppVersion - "1.0.0" (the program's own, from FluentSwitcher.exe)
;   SourceDir  - the folder with FluentSwitcher.exe, flags\, LICENSE and THIRD-PARTY-NOTICES.txt
;   OutputDir  - where FluentSwitcher-<version>-setup.exe goes
; For the current user, without administrator rights: into %LOCALAPPDATA%\Programs\FluentSwitcher, a shortcut
; in the Start menu, removal in Settings - Apps. The settings file (FluentSwitcher.json) stays on an update.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\release\FluentSwitcher"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\release"
#endif

[Setup]
AppId={{6BF2E579-D7C0-47DA-83E4-DB147C02980D}
AppName=FluentSwitcher
AppVersion={#AppVersion}
AppVerName=FluentSwitcher {#AppVersion}
AppPublisher=Dmitry Novikov
AppPublisherURL=https://github.com/DmitryN71/FluentSwitcher
AppSupportURL=https://github.com/DmitryN71/FluentSwitcher/issues
AppUpdatesURL=https://github.com/DmitryN71/FluentSwitcher/releases
AppCopyright=GPL-3.0. Based on SimpleSwitcher by Aegel5
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\FluentSwitcher
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=FluentSwitcher-{#AppVersion}-setup
SetupIconFile=..\src\res\app.ico
UninstallDisplayIcon={app}\FluentSwitcher.exe
UninstallDisplayName=FluentSwitcher
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; A running FluentSwitcher of 1.0 and newer is closed by "FluentSwitcher.exe --quit" (PrepareToInstall);
; older test builds do not know it, Windows' Restart Manager offers to close them.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\FluentSwitcher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\flags\*"; DestDir: "{app}\flags"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; What older test builds and SimpleSwitcher left in the same folder.
Type: files; Name: "{app}\FluentSwitcherSettings.exe"
Type: files; Name: "{app}\SimpleSwitcher.exe"
Type: files; Name: "{app}\imgui.ini"
Type: filesandordirs; Name: "{app}\flags\Fluent"
Type: filesandordirs; Name: "{app}\flags\Round"
Type: filesandordirs; Name: "{app}\flags\Square"

[Icons]
Name: "{autoprograms}\FluentSwitcher"; Filename: "{app}\FluentSwitcher.exe"

[Run]
Filename: "{app}\FluentSwitcher.exe"; Description: "{cm:LaunchProgram,FluentSwitcher}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Close it, then remove its autostart (the Run value or the scheduled task - that one asks for administrator rights).
Filename: "{app}\FluentSwitcher.exe"; Parameters: "--quit"; RunOnceId: "Quit"; Flags: runhidden waituntilterminated
Filename: "{app}\FluentSwitcher.exe"; Parameters: "--cleanup"; RunOnceId: "Cleanup"; Flags: runhidden waituntilterminated

[UninstallDelete]
Type: files; Name: "{app}\FluentSwitcher.json"
Type: files; Name: "{app}\update.json"
Type: filesandordirs; Name: "{app}\log"
Type: dirifempty; Name: "{app}"

[Code]
// Before files are replaced: close a running FluentSwitcher of this folder. Only 1.0 and newer know --quit;
// an older test build would start a second copy instead.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Exe: String;
  VersionMS, VersionLS: Cardinal;
  Major: Cardinal;
  ResultCode: Integer;
begin
  Result := '';
  Exe := ExpandConstant('{app}\FluentSwitcher.exe');
  if FileExists(Exe) and GetVersionNumbers(Exe, VersionMS, VersionLS) then
  begin
    Major := VersionMS shr 16;
    // The test builds before 1.0 carry SimpleSwitcher's number, 7.0.6.
    if (Major >= 1) and (Major < 7) then
      Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
