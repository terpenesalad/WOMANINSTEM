; Inno Setup script for WOMANINSTEM
; Built by CI:  ISCC /DAppVersion=3.2.0 /DSourceDir=dist\WOMANINSTEM /Odist packaging\installer.iss

#ifndef AppVersion
  #define AppVersion "3.2.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\WOMANINSTEM"
#endif

[Setup]
AppId={{6E0B7F3A-5A0C-4C7E-9B7E-57E3A1D2C0FE}
AppName=WOMANINSTEM
AppVersion={#AppVersion}
AppVerName=WOMANINSTEM {#AppVersion}
AppPublisher=WOMANINSTEM
AppPublisherURL=https://github.com/terpenesalad/WOMANINSTEM
AppSupportURL=https://github.com/terpenesalad/WOMANINSTEM/issues
DefaultDirName={autopf}\WOMANINSTEM
DefaultGroupName=WOMANINSTEM
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=WOMANINSTEM-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\WOMANINSTEM.exe
LicenseFile={#SourceDir}\LICENSE.txt
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "openwith"; Description: "Add ""Open with WOMANINSTEM"" for MP3, FLAC and WAV files"; GroupDescription: "File types:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\WOMANINSTEM"; Filename: "{app}\WOMANINSTEM.exe"
Name: "{group}\Read me"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall WOMANINSTEM"; Filename: "{uninstallexe}"
Name: "{autodesktop}\WOMANINSTEM"; Filename: "{app}\WOMANINSTEM.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\Applications\WOMANINSTEM.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\WOMANINSTEM.exe"" ""%1"""; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.mp3\OpenWithList\WOMANINSTEM.exe"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.flac\OpenWithList\WOMANINSTEM.exe"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.wav\OpenWithList\WOMANINSTEM.exe"; Flags: uninsdeletekey; Tasks: openwith
; Studio songs (.wisproj) open in WOMANINSTEM
Root: HKA; Subkey: "Software\Classes\.wisproj"; ValueType: string; ValueName: ""; ValueData: "WOMANINSTEM.Song"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\WOMANINSTEM.Song"; ValueType: string; ValueName: ""; ValueData: "WOMANINSTEM Song"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\WOMANINSTEM.Song\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\WOMANINSTEM.exe,0"
Root: HKA; Subkey: "Software\Classes\WOMANINSTEM.Song\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\WOMANINSTEM.exe"" ""%1"""

[Run]
Filename: "{app}\WOMANINSTEM.exe"; Description: "{cm:LaunchProgram,WOMANINSTEM}"; Flags: nowait postinstall skipifsilent
