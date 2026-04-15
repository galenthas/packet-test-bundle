#define MyAppName "Packet Test Bundle"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#define MyAppExe "PacketTestBundle.exe"
#define MyAppPublisher "galenthas"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=installer_out
OutputBaseFilename=PacketTestBundle_v{#MyAppVersion}_Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; Main executable
Source: "build_new\PacketTestBundle.exe"; DestDir: "{app}"; Flags: ignoreversion

; Bundled tools
Source: "..\bin\iperf2\*"; DestDir: "{app}\bin\iperf2"; Flags: ignoreversion recursesubdirs
Source: "..\bin\iperf3\*"; DestDir: "{app}\bin\iperf3"; Flags: ignoreversion recursesubdirs
Source: "..\bin\tshark\*"; DestDir: "{app}\bin\tshark"; Flags: ignoreversion recursesubdirs

; Npcap installer (extracted after install, not left behind)
Source: "npcap-installer.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Code]
function NpcapInstalled(): Boolean;
var
  version: String;
begin
  Result := RegQueryStringValue(HKLM, 'SOFTWARE\Npcap', 'Version', version)
         or RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Npcap', 'Version', version);
end;

procedure InstallNpcap();
var
  resultCode: Integer;
begin
  if NpcapInstalled() then
  begin
    Log('Npcap already installed, skipping.');
    Exit;
  end;
  MsgBox('Npcap will now be installed. It is required for packet capture. Please follow the installer steps.', mbInformation, MB_OK);
  Log('Installing Npcap...');
  if not Exec(ExpandConstant('{tmp}\npcap-installer.exe'), '', '', SW_SHOW, ewWaitUntilTerminated, resultCode) then
  begin
    MsgBox('Npcap installation failed. Packet capture may not work.', mbError, MB_OK);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    InstallNpcap();
end;
