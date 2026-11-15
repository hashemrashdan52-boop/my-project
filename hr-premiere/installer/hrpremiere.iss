; Inno Setup script – builds HR-Premiere-Setup.exe from the packaged folder.
; Usage:  iscc /DAppVersion=0.1.0 /DSourceDir=C:\path\to\HR-Premiere hrpremiere.iss
;         (add /DNO_ARABIC=1 for an English-only installer)
;
; The first page of the installer lets the user choose Arabic or English.  The choice is also written to
; HKCU\Software\HR\HRPremiere (value "language"), where the program reads it, so the program starts in the same
; language and does not ask again.  It can be changed later in Edit > Preferences.
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
; always ask which language to use (also when updating): the wish of the owner is "choose when installing"
ShowLanguageDialog=yes
UsePreviousLanguage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
#ifndef NO_ARABIC
Name: "arabic"; MessagesFile: "Arabic.isl"
#endif

[CustomMessages]
english.LangCode=en
#ifndef NO_ARABIC
arabic.LangCode=ar
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\HR Premiere"; Filename: "{app}\HRPremiere.exe"
Name: "{autodesktop}\HR Premiere"; Filename: "{app}\HRPremiere.exe"; Tasks: desktopicon

[Registry]
; the language chosen on the first page of this installer is the language of the program
Root: HKCU; Subkey: "Software\HR\HRPremiere"; ValueType: string; ValueName: "language"; ValueData: "{cm:LangCode}"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\HRPremiere.exe"; Description: "{cm:LaunchProgram,HR Premiere}"; Flags: nowait postinstall skipifsilent
