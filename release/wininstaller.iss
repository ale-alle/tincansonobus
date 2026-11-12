; CanDuet installer (Tin Can Studios fork of SonoBus, GPLv3). Windows 10/11 64-bit, standalone app only.
; Build with release\build-installer.ps1 (stages the files and runs Inno Setup 6's ISCC):
;   unsigned (pilot):  release\build-installer.ps1
;   signed (later):    release\build-installer.ps1 -Sign   (needs a code-signing certificate, see BUILD-WINDOWS.md)
;
; Installs for the current user only (%LOCALAPPDATA%\Programs\CanDuet): no administrator prompt, which for an
; unsigned app would say "Unknown publisher". Installing a new version over the old one keeps settings
; (they live in %APPDATA%, not in the install folder) and the uninstaller leaves them too.

#ifndef SBVERSION
  #error Define SBVERSION (release\build-installer.ps1 does this)
#endif

[Setup]
AppName=CanDuet
AppVersion={#SBVERSION}
AppVerName=CanDuet {#SBVERSION}
AppPublisher=Tin Can Studios
AppPublisherURL=https://tincanstudios.ca
AppSupportURL=https://tincanstudios.ca/help
AppUpdatesURL=https://tincanstudios.ca/download
AppId={{6F0C2B7E-4C1D-4B8E-9A3B-CA0D0E7C2D11}
VersionInfoVersion={#SBVERSION}
VersionInfoProductName=CanDuet
MinVersion=10.0
WizardStyle=modern
PrivilegesRequired=lowest
DefaultDirName={autopf}\CanDuet
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName=CanDuet
DisableWelcomePage=yes
DisableReadyPage=yes
UninstallDisplayIcon={app}\CanDuet.exe
UninstallDisplayName=CanDuet
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=CanDuet-{#SBVERSION}-Setup
SetupLogging=yes
; a running CanDuet (for example left open after a lesson) is closed before its files are replaced
CloseApplications=force
RestartApplications=no
#ifdef SIGN
; "signtool" is defined on the ISCC command line by build-installer.ps1 -Sign
SignTool=signtool $f
SignedUninstaller=yes
#endif

[Files]
#ifdef SIGN
Source: "CanDuet\CanDuet.exe"; DestDir: "{app}"; Flags: ignoreversion signonce
#else
Source: "CanDuet\CanDuet.exe"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "CanDuet\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "gpl-3.0.txt"; DestDir: "{app}"; DestName: "LICENSE.txt"
Source: "SOURCE.txt"; DestDir: "{app}"

[Icons]
Name: "{autoprograms}\CanDuet"; Filename: "{app}\CanDuet.exe"
Name: "{autodesktop}\CanDuet"; Filename: "{app}\CanDuet.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"

[Registry]
; The website's "Join lesson" button opens canduet://join?ticket=... (T-018). HKA is HKCU\Software\Classes
; for this per-user install. sonobus:// is deliberately not registered, so links meant for a real
; SonoBus install never open CanDuet.
Root: HKA; Subkey: "Software\Classes\canduet"; ValueType: string; ValueData: "URL:CanDuet Protocol"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\canduet"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\canduet\DefaultIcon"; ValueType: string; ValueData: """{app}\CanDuet.exe"",0"
Root: HKA; Subkey: "Software\Classes\canduet\shell\open\command"; ValueType: string; ValueData: """{app}\CanDuet.exe"" ""%1"""

[Run]
Filename: "{app}\CanDuet.exe"; Description: "Start CanDuet"; Flags: nowait postinstall skipifsilent

[Code]
const
  // Microsoft Edge WebView2 Runtime (Evergreen) client id
  WebView2Client = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';

function HasWebView2Key(RootKey: Integer; SubKey: String): Boolean;
var
  Version: String;
begin
  Result := RegQueryStringValue(RootKey, SubKey + WebView2Client, 'pv', Version)
            and (Version <> '') and (Version <> '0.0.0.0');
end;

function HasWebView2: Boolean;
begin
  Result := HasWebView2Key(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\')
         or HasWebView2Key(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\')
         or HasWebView2Key(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
begin
  // Windows 11 always has WebView2; some Windows 10 PCs don't. Audio works without it, video doesn't.
  if (CurStep = ssPostInstall) and (not WizardSilent) and (not HasWebView2) then
  begin
    if MsgBox('CanDuet shows video with Microsoft Edge WebView2, which isn''t on this PC yet.' + #13#10#13#10 +
              'Lessons work without it, but you won''t see video. Open Microsoft''s download page now? ' +
              'Choose "Evergreen Bootstrapper" there.', mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', 'https://developer.microsoft.com/microsoft-edge/webview2/', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
  end;
end;
