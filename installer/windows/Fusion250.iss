; Basemental FG 250 Windows installer (Inno Setup 6)
; Compile: iscc /DAppVersion=1.2.0 /DArtDir=<artefacts\Release> /DSrcDir=<repo> installer\windows\Fusion250.iss

#ifndef AppVersion
  #define AppVersion "1.3.0"
#endif

[Setup]
AppId={{8F2C6A51-3B2E-4C3A-9E51-25F0FA250001}
AppName=Basemental FG 250
AppVersion={#AppVersion}
AppVerName=Basemental FG 250 {#AppVersion}
AppPublisher=Basemental
DefaultDirName={autopf}\Basemental FG 250
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir={#SrcDir}\dist
OutputBaseFilename=BasementalFG250-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Basemental FG 250

[Types]
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Ableton, Cubase, Studio One, Reaper, FL Studio...)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full

[Files]
Source: "{#ArtDir}\VST3\Basemental FG 250.vst3\*"; DestDir: "{commoncf64}\VST3\Basemental FG 250.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#ArtDir}\Standalone\Basemental FG 250.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#SrcDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Basemental FG 250"; Filename: "{app}\Basemental FG 250.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Basemental FG 250.vst3"
