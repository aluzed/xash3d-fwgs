; xash3d.iss - Windows installer of Xash3D FWGS with Half-Life and
; Counter-Strike (story 052). stage.ps1 gathers the files into stage\ first;
; the game data is copied at install time from the player's Steam folder.
;
;   iscc /DVersion=<version> xash3d.iss

#ifndef Version
  #define Version "0.21"
#endif

[Setup]
AppId={{6C2F4E1A-8B7D-4C3E-9A15-2D0F7B4E9C61}
AppName=Xash3D FWGS
AppVersion={#Version}
AppPublisher=aluzed
AppPublisherURL=https://github.com/aluzed/xash3d-fwgs
; per user: the engine writes its settings, saves and screenshots next to itself
PrivilegesRequired=lowest
DefaultDirName={userpf}\Xash3D FWGS
DisableProgramGroupPage=yes
DefaultGroupName=Xash3D FWGS
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=license.txt
SetupIconFile=installer.ico
WizardSmallImageFile=wizard-small-55.bmp,wizard-small-110.bmp
UninstallDisplayIcon={app}\installer.ico
UninstallDisplayName=Xash3D FWGS (Half-Life, Counter-Strike)
WizardStyle=modern
OutputDir=output
OutputBaseFilename=Xash3D-FWGS-Setup-{#Version}
Compression=lzma2/max
SolidCompression=yes

[Types]
Name: "full"; Description: "Half-Life and Counter-Strike"
Name: "hl"; Description: "Half-Life only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "hl"; Description: "Half-Life (needed by Counter-Strike too)"; Types: full hl custom; Flags: fixed
Name: "cs"; Description: "Counter-Strike, with bots"; Types: full custom
Name: "server"; Description: "Dedicated servers (Half-Life, Counter-Strike)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcuts"; GroupDescription: "Shortcuts:"

[Files]
; the game data first, from the player's Steam folder, without Valve's
; libraries: the 64-bit ones below replace them
Source: "{code:GetGameDir}\valve\*"; DestDir: "{app}\valve"; Excludes: "*.dll,*.so,*.dylib,*.exe"; \
  Flags: external recursesubdirs createallsubdirs ignoreversion; ExternalSize: 540000000; Components: hl
Source: "{code:GetGameDir}\cstrike\*"; DestDir: "{app}\cstrike"; Excludes: "*.dll,*.so,*.dylib,*.exe"; \
  Flags: external recursesubdirs createallsubdirs ignoreversion; ExternalSize: 310000000; Components: cs
; the engine and Half-Life's libraries
Source: "installer.ico"; DestDir: "{app}"; Flags: ignoreversion; Components: hl
Source: "stage\*"; DestDir: "{app}"; Excludes: "\cstrike\*,\xash-dedicated.exe"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: hl
; the dedicated server, next to the client: same filesystem and game libraries
Source: "stage\xash-dedicated.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: server
; Counter-Strike's libraries, its extras and bots
Source: "stage\cstrike\*"; DestDir: "{app}\cstrike"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: cs

[Icons]
Name: "{group}\Half-Life"; Filename: "{app}\xash3d.exe"; Parameters: "-game valve"; WorkingDir: "{app}"; IconFilename: "{app}\valve\game.ico"; Components: hl
Name: "{group}\Counter-Strike"; Filename: "{app}\xash3d.exe"; Parameters: "-game cstrike -dll @yapb"; WorkingDir: "{app}"; IconFilename: "{app}\cstrike\game.ico"; Components: cs
; the options of the dedicated server stories (038-048), as scripts/server/run.sh
Name: "{group}\Counter-Strike server"; Filename: "{app}\xash-dedicated.exe"; Parameters: "-game cstrike -dll @yapb -port 27015 +maxplayers 20 +sys_scheduler 1 +sv_snapshot_spread 1 +sv_log_async 1 +sv_client_lookup_cache 1 -netthread +sv_a2s_cache 1 -netsend +map de_dust2"; WorkingDir: "{app}"; IconFilename: "{app}\cstrike\game.ico"; Components: server and cs
Name: "{group}\Half-Life server"; Filename: "{app}\xash-dedicated.exe"; Parameters: "-game valve -port 27016 +maxplayers 16 +map crossfire"; WorkingDir: "{app}"; IconFilename: "{app}\valve\game.ico"; Components: server
Name: "{group}\Uninstall Xash3D FWGS"; Filename: "{uninstallexe}"
Name: "{userdesktop}\Half-Life (Xash3D)"; Filename: "{app}\xash3d.exe"; Parameters: "-game valve"; WorkingDir: "{app}"; IconFilename: "{app}\valve\game.ico"; Components: hl; Tasks: desktopicon
Name: "{userdesktop}\Counter-Strike (Xash3D)"; Filename: "{app}\xash3d.exe"; Parameters: "-game cstrike -dll @yapb"; WorkingDir: "{app}"; IconFilename: "{app}\cstrike\game.ico"; Components: cs; Tasks: desktopicon

[Run]
Filename: "{app}\xash3d.exe"; Parameters: "-game cstrike -dll @yapb"; WorkingDir: "{app}"; Description: "Launch Counter-Strike"; Flags: nowait postinstall skipifsilent; Components: cs
Filename: "{app}\xash3d.exe"; Parameters: "-game valve"; WorkingDir: "{app}"; Description: "Launch Half-Life"; Flags: nowait postinstall skipifsilent unchecked; Components: hl

[UninstallDelete]
; what the engine makes as it runs, always regenerated
Type: filesandordirs; Name: "{app}\*\.fontcache"
Type: files; Name: "{app}\*\vulkan_pipelines.bin"
Type: files; Name: "{app}\engine.log"

[Code]
function GetSystemMetrics( nIndex: Integer ): Integer;
  external 'GetSystemMetrics@user32.dll stdcall';

var
  GamePage: TInputDirWizardPage;

// the Half-Life folder of a Steam library, when Half-Life is installed in it
function GameInLibrary( Lib: String ): String;
begin
  Result := '';
  Lib := RemoveBackslashUnlessRoot( Lib );
  if FileExists( Lib + '\steamapps\common\Half-Life\valve\liblist.gam' ) then
    Result := Lib + '\steamapps\common\Half-Life';
end;

// Steam's own folder, then the libraries of libraryfolders.vdf
function FindSteamGame(): String;
var
  Steam, Line, Path: String;
  Lines: TArrayOfString;
  I, P: Integer;
begin
  Result := '';
  if not RegQueryStringValue( HKCU, 'Software\Valve\Steam', 'SteamPath', Steam ) then
    if not RegQueryStringValue( HKLM, 'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath', Steam ) then
      Exit;
  StringChangeEx( Steam, '/', '\', True );

  Result := GameInLibrary( Steam );
  if Result <> '' then
    Exit;

  if not LoadStringsFromFile( Steam + '\steamapps\libraryfolders.vdf', Lines ) then
    Exit;

  // lines like:  "path"		"D:\\SteamLibrary"
  for I := 0 to GetArrayLength( Lines ) - 1 do
  begin
    Line := Trim( Lines[I] );
    if Pos( '"path"', Lowercase( Line )) = 1 then
    begin
      Path := Trim( Copy( Line, 7, Length( Line )));
      P := Pos( '"', Path );
      if P > 0 then
      begin
        Path := Copy( Path, P + 1, Length( Path ));
        P := Pos( '"', Path );
        if P > 0 then
          Path := Copy( Path, 1, P - 1 );
        StringChangeEx( Path, '\\', '\', True );
        Result := GameInLibrary( Path );
        if Result <> '' then
          Exit;
      end;
    end;
  end;
end;

function GetGameDir( Param: String ): String;
begin
  Result := RemoveBackslashUnlessRoot( GamePage.Values[0] );
end;

function HasCounterStrike(): Boolean;
begin
  Result := FileExists( GetGameDir( '' ) + '\cstrike\liblist.gam' );
end;

procedure InitializeWizard();
begin
  GamePage := CreateInputDirPage( wpLicense,
    'Half-Life folder',
    'Where is Half-Life installed by Steam?',
    'The game data (the valve and cstrike folders) is copied from there. ' +
    'Counter-Strike is in the same folder when you own it.' + #13#10#13#10 +
    'Click Next to use this folder, or Browse to pick another one.',
    False, '' );
  GamePage.Add( 'Half-Life folder (it has the "valve" folder):' );
  GamePage.Values[0] := FindSteamGame();
end;

function NextButtonClick( CurPageID: Integer ): Boolean;
begin
  Result := True;
  if CurPageID = GamePage.ID then
  begin
    if not FileExists( GetGameDir( '' ) + '\valve\liblist.gam' ) then
    begin
      MsgBox( 'Half-Life was not found in this folder: it has no "valve\liblist.gam".' + #13#10#13#10 +
        'Pick the Half-Life folder of your Steam library, usually' + #13#10 +
        'C:\Program Files (x86)\Steam\steamapps\common\Half-Life', mbError, MB_OK );
      Result := False;
    end;
  end;
end;

procedure CurPageChanged( CurPageID: Integer );
begin
  // Counter-Strike only when the Steam folder has it
  if CurPageID = wpSelectComponents then
  begin
    WizardForm.ComponentsList.ItemEnabled[1] := HasCounterStrike();
    if not HasCounterStrike() then
      WizardForm.ComponentsList.Checked[1] := False;
  end;
end;

// video.cfg of a first run: the Vulkan renderer (the engine falls back to
// OpenGL on its own without it), fullscreen at the desktop's resolution;
// never over the player's own
procedure WriteVideoConfig( GameDir: String );
var
  Cfg: String;
begin
  Cfg := ExpandConstant( '{app}\' ) + GameDir + '\video.cfg';
  if FileExists( Cfg ) or not DirExists( ExpandConstant( '{app}\' ) + GameDir ) then
    Exit;
  SaveStringToFile( Cfg,
    '// first run settings of the installer, the game rewrites this file' + #13#10 +
    'r_refdll "vk"' + #13#10 +
    'fullscreen "1"' + #13#10 +
    'width "' + IntToStr( GetSystemMetrics( 0 )) + '"' + #13#10 +
    'height "' + IntToStr( GetSystemMetrics( 1 )) + '"' + #13#10, False );
end;

procedure CurStepChanged( CurStep: TSetupStep );
begin
  if CurStep = ssPostInstall then
  begin
    WriteVideoConfig( 'valve' );
    if WizardIsComponentSelected( 'cs' ) then
      WriteVideoConfig( 'cstrike' );
  end;
end;

procedure CurUninstallStepChanged( CurUninstallStep: TUninstallStep );
begin
  // what the player made stays unless asked: settings, saves, screenshots, demos
  if CurUninstallStep = usPostUninstall then
    if DirExists( ExpandConstant( '{app}' )) then
      if SuppressibleMsgBox( 'Also delete your settings, saved games, screenshots and demos?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO ) = IDYES then
        DelTree( ExpandConstant( '{app}' ), True, True, True );
end;
