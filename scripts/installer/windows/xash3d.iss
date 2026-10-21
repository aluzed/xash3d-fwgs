; xash3d.iss - Windows installer of Half-Life 1.5 and Counter-Strike 1.7,
; Xash3D FWGS with Half-Life and Counter-Strike (stories 052, 053).
; stage.ps1 gathers the files into stage\ first; the game data is copied at
; install time from the player's Steam folder.
;
;   iscc /DVersion=<version> xash3d.iss

#ifndef Version
  #define Version "0.21"
#endif
; the names the player reads (story 053); the game folders stay valve and
; cstrike, the ones servers and demos name
#ifndef HLName
  #define HLName "Half-Life 1.5"
#endif
#ifndef CSName
  #define CSName "Counter-Strike 1.7"
#endif
#define Product HLName + " and " + CSName

[Setup]
AppId={{6C2F4E1A-8B7D-4C3E-9A15-2D0F7B4E9C61}
AppName={#Product}
AppVersion={#Version}
AppVerName={#Product} (unofficial build {#Version})
AppPublisher=aluzed
AppPublisherURL=https://github.com/aluzed/xash3d-fwgs
; per user: the engine writes its settings, saves and screenshots next to itself
PrivilegesRequired=lowest
; an earlier set (052, "Xash3D FWGS") is upgraded in its own folder: same AppId
DefaultDirName={userpf}\{#HLName}
DisableProgramGroupPage=yes
DefaultGroupName={#Product}
UsePreviousGroup=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=license.txt
SetupIconFile=installer.ico
WizardSmallImageFile=wizard-small-55.bmp,wizard-small-110.bmp
UninstallDisplayIcon={app}\installer.ico
UninstallDisplayName={#Product}
WizardStyle=modern
OutputDir=output
OutputBaseFilename={#Product} Setup
Compression=lzma2/max
SolidCompression=yes

[Messages]
; most of the install is the copy of the game data from Steam
StatusExtractFiles=Copying files...

[Types]
Name: "full"; Description: "{#HLName} and {#CSName}"
Name: "hl"; Description: "{#HLName} only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "hl"; Description: "{#HLName} (needed by {#CSName} too)"; Types: full hl custom; Flags: fixed
Name: "cs"; Description: "{#CSName}, with bots"; Types: full custom
Name: "server"; Description: "Dedicated servers ({#HLName}, {#CSName})"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcuts"; GroupDescription: "Shortcuts:"

[Files]
; the game data first, from the player's Steam folder, without Valve's
; libraries: the 64-bit ones below replace them
Source: "{code:GetGameDir}\valve\*"; DestDir: "{app}\valve"; Excludes: "*.dll,*.so,*.dylib,*.exe,\config.cfg"; \
  Flags: external recursesubdirs createallsubdirs ignoreversion; ExternalSize: 540000000; Components: hl
Source: "{code:GetGameDir}\cstrike\*"; DestDir: "{app}\cstrike"; Excludes: "*.dll,*.so,*.dylib,*.exe,\config.cfg"; \
  Flags: external recursesubdirs createallsubdirs ignoreversion; ExternalSize: 310000000; Components: cs
; the player's Steam settings (keys, mouse...) on a first install only: a
; reinstall keeps the ones of this set, and the uninstaller asks for them
Source: "{code:GetGameDir}\valve\config.cfg"; DestDir: "{app}\valve"; \
  Flags: external onlyifdoesntexist skipifsourcedoesntexist uninsneveruninstall; Components: hl
Source: "{code:GetGameDir}\cstrike\config.cfg"; DestDir: "{app}\cstrike"; \
  Flags: external onlyifdoesntexist skipifsourcedoesntexist uninsneveruninstall; Components: cs
; the engine and Half-Life's libraries
Source: "installer.ico"; DestDir: "{app}"; Flags: ignoreversion; Components: hl
Source: "stage\*"; DestDir: "{app}"; Excludes: "\cstrike,\tools,\xash-dedicated.exe"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: hl
; the dedicated server, next to the client: same filesystem and game libraries
Source: "stage\xash-dedicated.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: server
; Counter-Strike's libraries, its extras and bots
Source: "stage\cstrike\*"; DestDir: "{app}\cstrike"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: cs
; writes de_dust2 with the small crates of story 065 from the player's map;
; next to xash3d.exe for the MSVC runtime, removed once installed
Source: "stage\tools\patch_dust2.exe"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall; Components: cs

[InstallDelete]
; the shortcuts of an earlier set, under the names of 052
Type: filesandordirs; Name: "{userprograms}\Xash3D FWGS"
Type: files; Name: "{userdesktop}\Half-Life (Xash3D).lnk"
Type: files; Name: "{userdesktop}\Counter-Strike (Xash3D).lnk"

[Icons]
Name: "{group}\{#HLName}"; Filename: "{app}\xash3d.exe"; Parameters: "-game valve"; WorkingDir: "{app}"; IconFilename: "{app}\valve\game.ico"; Components: hl
Name: "{group}\{#CSName}"; Filename: "{app}\xash3d.exe"; Parameters: "-game cstrike -dll @yapb"; WorkingDir: "{app}"; IconFilename: "{app}\cstrike\game.ico"; Components: cs
; the options of the dedicated server stories (038-048), as scripts/server/run.sh
Name: "{group}\{#CSName} server"; Filename: "{app}\xash-dedicated.exe"; Parameters: "-game cstrike -dll @yapb -port 27015 +maxplayers 20 +sys_scheduler 1 +sv_snapshot_spread 1 +sv_log_async 1 +sv_client_lookup_cache 1 -netthread +sv_a2s_cache 1 -netsend +map de_dust2"; WorkingDir: "{app}"; IconFilename: "{app}\cstrike\game.ico"; Components: server and cs
Name: "{group}\{#HLName} server"; Filename: "{app}\xash-dedicated.exe"; Parameters: "-game valve -port 27016 +maxplayers 16 +map crossfire"; WorkingDir: "{app}"; IconFilename: "{app}\valve\game.ico"; Components: server
Name: "{group}\Uninstall {#Product}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#HLName}"; Filename: "{app}\xash3d.exe"; Parameters: "-game valve"; WorkingDir: "{app}"; IconFilename: "{app}\valve\game.ico"; Components: hl; Tasks: desktopicon
Name: "{userdesktop}\{#CSName}"; Filename: "{app}\xash3d.exe"; Parameters: "-game cstrike -dll @yapb"; WorkingDir: "{app}"; IconFilename: "{app}\cstrike\game.ico"; Components: cs; Tasks: desktopicon

[Run]
; a de_dust2 that isn't Counter-Strike's own is left as it is
Filename: "{app}\patch_dust2.exe"; Parameters: """{code:GetGameDir}\cstrike\maps\de_dust2.bsp"" ""{app}\cstrike\maps\de_dust2.bsp"""; \
  WorkingDir: "{app}"; StatusMsg: "Adding the crates to de_dust2..."; Flags: runhidden waituntilterminated; Components: cs
Filename: "{app}\xash3d.exe"; Parameters: "-game cstrike -dll @yapb"; WorkingDir: "{app}"; Description: "Launch {#CSName}"; Flags: nowait postinstall skipifsilent; Components: cs
Filename: "{app}\xash3d.exe"; Parameters: "-game valve"; WorkingDir: "{app}"; Description: "Launch {#HLName}"; Flags: nowait postinstall skipifsilent unchecked; Components: hl

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
    '{#Product} is an unofficial set made by fans, not by Valve: it needs ' +
    'your own Steam copy of the games.' + #13#10#13#10 +
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

// config.cfg of a first install: the player's Steam one, or Valve's
// default keys when the game never ran under Steam, with the settings of
// this set: the HUD in UTF-8, F12 for a screenshot as well as F5 (unless
// the player uses F12). A reinstall keeps the set's own config.cfg.
procedure WriteGameConfig( GameDir: String; Existed: Boolean );
var
  Cfg: String;
  Text: AnsiString;
begin
  Cfg := ExpandConstant( '{app}\' ) + GameDir + '\config.cfg';
  if Existed or not DirExists( ExpandConstant( '{app}\' ) + GameDir ) then
    Exit;
  if not LoadStringFromFile( Cfg, Text ) then
    Text := 'exec default.cfg' + #13#10;
  Text := Text + #13#10 + '// settings of the installer' + #13#10 + 'hud_utf8 "1"' + #13#10;
  if ( Pos( 'bind "f12"', Lowercase( Text )) = 0 ) and ( Pos( 'bind f12', Lowercase( Text )) = 0 ) then
    Text := Text + 'bind "F12" "snapshot"' + #13#10;
  SaveStringToFile( Cfg, Text, False );
end;

// the game's name in the installed copy of liblist.gam (the window, the
// menus), and its version: Valve's file in Steam is never touched, the
// game folder and every other key stay as they are
procedure WriteGameTitle( GameDir, Title, Version: String );
var
  List: String;
  Lines: TArrayOfString;
  I: Integer;
  Key: String;
begin
  List := ExpandConstant( '{app}\' ) + GameDir + '\liblist.gam';
  if not LoadStringsFromFile( List, Lines ) then
    Exit;
  for I := 0 to GetArrayLength( Lines ) - 1 do
  begin
    Key := Lowercase( Trim( Lines[I] ));
    if ( Pos( 'game ', Key ) = 1 ) or ( Pos( 'game' + #9, Key ) = 1 ) or ( Pos( 'game"', Key ) = 1 ) or ( Pos( '"game"', Key ) = 1 ) then
      Lines[I] := 'game "' + Title + '"'
    else if ( Version <> '' ) and (( Pos( 'version ', Key ) = 1 ) or ( Pos( 'version' + #9, Key ) = 1 ) or ( Pos( '"version"', Key ) = 1 )) then
      Lines[I] := 'version "' + Version + '"';
  end;
  SaveStringsToFile( List, Lines, False );
end;

var
  HadValveConfig, HadCstrikeConfig: Boolean;

procedure CurStepChanged( CurStep: TSetupStep );
begin
  if CurStep = ssInstall then
  begin
    HadValveConfig := FileExists( ExpandConstant( '{app}\valve\config.cfg' ));
    HadCstrikeConfig := FileExists( ExpandConstant( '{app}\cstrike\config.cfg' ));
  end;
  if CurStep = ssPostInstall then
  begin
    WriteGameTitle( 'valve', '{#HLName}', '' );
    WriteVideoConfig( 'valve' );
    WriteGameConfig( 'valve', HadValveConfig );
    if WizardIsComponentSelected( 'cs' ) then
    begin
      WriteGameTitle( 'cstrike', '{#CSName}', '1.7' );
      WriteVideoConfig( 'cstrike' );
      WriteGameConfig( 'cstrike', HadCstrikeConfig );
    end;
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
