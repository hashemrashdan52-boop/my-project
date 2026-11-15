; Inno Setup script – builds HR-Premiere-Setup.exe from the packaged folder.
; Usage:  iscc /DAppVersion=0.1.0 /DSourceDir=C:\path\to\HR-Premiere hrpremiere.iss
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\HR-Premiere"
#endif

[Setup]
AppId={{8F6C5C4E-3D0B-4F55-9A0E-4A1A0D2B7A11}
AppName=HR Premiere
AppVersion={#AppVersion}
AppPublisher=HR
DefaultDirName={autopf}\HR Premiere
DefaultGroupName=HR Premiere
UninstallDisplayIcon={app}\HRPremiere.exe
SetupIconFile=..\resources\hrpremiere.ico
OutputDir=..\dist
OutputBaseFilename=HR-Premiere-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\HR Premiere"; Filename: "{app}\HRPremiere.exe"
Name: "{autodesktop}\HR Premiere"; Filename: "{app}\HRPremiere.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\HRPremiere.exe"; Description: "Launch HR Premiere"; Flags: nowait postinstall skipifsilent
