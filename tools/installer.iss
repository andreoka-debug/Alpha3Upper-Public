#ifndef ReleaseDir
  #error ReleaseDir must point to the audited public dist folder.
#endif
#ifndef OutputDir
  #define OutputDir "..\build\installer"
#endif
#ifndef IconFile
  #define IconFile "..\assets\app.ico"
#endif
#ifdef InstallerValidation
  #define ShortcutName "Alpha3Upper Public Installer Test"
#else
  #define ShortcutName "Alpha3Upper Public"
#endif
[Setup]
#ifdef InstallerValidation
AppId={{1F46CC38-A37C-4E80-8D82-543BEAD05F43}
AppName=Alpha3Upper Public Installer Test
DefaultGroupName=Alpha3Upper Public Installer Test
#else
AppId={{119FE33A-10D3-4209-98F1-7C6D2B06D1B2}
AppName=Alpha3Upper Public
DefaultGroupName=Alpha3Upper Public
#endif
AppVersion=0.16.0
AppVerName=Alpha3Upper Public 0.16.0
VersionInfoVersion=0.16.0.0
AppPublisher=Andre Oka
AppPublisherURL=https://github.com/andreoka-debug/Alpha3Upper-Public
AppSupportURL=https://github.com/andreoka-debug/Alpha3Upper-Public/issues
AppUpdatesURL=https://github.com/andreoka-debug/Alpha3Upper-Public/releases
DefaultDirName={localappdata}\Programs\Alpha3UpperPublic
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Alpha3Upper-Public-Setup-v0.16.0-Windows-x64
SetupIconFile={#IconFile}
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Alpha3Upper Public
UninstallDisplayIcon={app}\Alpha3Upper.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
[Files]
Source: "{#ReleaseDir}\Alpha3Upper.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Alpha3Upper-Core.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
[Icons]
Name: "{group}\{#ShortcutName}"; Filename: "{app}\Alpha3Upper.exe"; WorkingDir: "{app}"; IconFilename: "{app}\Alpha3Upper.exe"
Name: "{userdesktop}\{#ShortcutName}"; Filename: "{app}\Alpha3Upper.exe"; WorkingDir: "{app}"; IconFilename: "{app}\Alpha3Upper.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\Alpha3Upper.exe"; Description: "{cm:LaunchProgram,Alpha3Upper Public}"; Flags: nowait postinstall skipifsilent
; Imported resources and settings live in a separate per-user cache.
; This installer neither ships them nor removes them on uninstall.
