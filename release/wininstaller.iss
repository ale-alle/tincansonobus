; CanDuet installer (Tin Can Studios fork of SonoBus, GPLv3).
; Windows 10/11 64-bit, standalone app only - lessons don't need the VST/AAX plugins.
; Build: release\distwin.sh <version> <certpassword>   (stages files into CanDuet\ and runs iscc)

[Setup]
AppName=CanDuet
AppVersion={#SBVERSION}
AppPublisher=Tin Can Studios
AppPublisherURL=https://tincanstudios.ca
AppId={{6F0C2B7E-4C1D-4B8E-9A3B-CA0D0E7C2D11}
MinVersion=10.0
WizardStyle=modern
DefaultDirName={autopf}\CanDuet
DefaultGroupName=CanDuet
UninstallDisplayIcon={app}\CanDuet.exe
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=CanDuet-{#SBVERSION}-Installer
LicenseFile=gpl-3.0.txt
SetupLogging=yes
SignTool=signtool $f
SignedUninstaller=yes
DisableReadyPage=true
DisableWelcomePage=yes
DisableDirPage=no

[Files]
Source: "CanDuet\CanDuet.exe"; DestDir: "{app}"; Flags: ignoreversion signonce
Source: "CanDuet\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "gpl-3.0.txt"; DestDir: "{app}"; DestName: "LICENSE.txt"

[Icons]
Name: "{group}\CanDuet"; Filename: "{app}\CanDuet.exe"
Name: "{group}\Uninstall CanDuet"; Filename: "{app}\unins000.exe"
Name: "{autodesktop}\CanDuet"; Filename: "{app}\CanDuet.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"

[Registry]
; The website's "Join lesson" button opens canduet://join?ticket=... (T-018).
; sonobus:// is deliberately not registered so we never take over links meant for a real SonoBus install.
Root: HKCR; Subkey: "canduet"; ValueType: "string"; ValueData: "URL:CanDuet Protocol"; Flags: uninsdeletekey
Root: HKCR; Subkey: "canduet"; ValueType: "string"; ValueName: "URL Protocol"; ValueData: ""
Root: HKCR; Subkey: "canduet\DefaultIcon"; ValueType: "string"; ValueData: "{app}\CanDuet.exe,0"
Root: HKCR; Subkey: "canduet\shell\open\command"; ValueType: "string"; ValueData: """{app}\CanDuet.exe"" ""%1"""

[Code]
var
  OkToCopyLog : Boolean;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then
    OkToCopyLog := True;
end;

procedure DeinitializeSetup();
begin
  if OkToCopyLog then
    FileCopy (ExpandConstant ('{log}'), ExpandConstant ('{app}\InstallationLogFile.log'), FALSE);
  RestartReplace (ExpandConstant ('{log}'), '');
end;

[UninstallDelete]
Type: files; Name: "{app}\InstallationLogFile.log"
