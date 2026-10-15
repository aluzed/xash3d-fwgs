<#
stage.ps1 - gathers the files of the Windows installer (story 052)

Puts the 64-bit engine, the game libraries of hlsdk-portable (Half-Life) and
cs16-client (Counter-Strike), SDL2 and the MSVC runtime in one folder laid
out as the installed game, then the installer script (xash3d.iss) packs it.
The game data isn't staged: the installer copies it from the player's Steam
folder.

  -Engine   the engine installed by waf (waf install --destdir=...), x64
  -Hlsdk    hlsdk-portable installed by CMake (has valve\cl_dlls, valve\dlls)
  -Cs16     cs16-client installed by CMake (has cstrike\...)
  -Sdl2     SDL2.dll, x64
  -Out      the staging folder, emptied first
#>
param(
	[string]$Engine = "C:\xash-dev\win64",
	[string]$Hlsdk = "C:\xash-dev\stage-hl",
	[string]$Cs16 = "C:\xash-dev\stage-cs",
	[string]$Sdl2 = "$PSScriptRoot\..\..\..\SDL2_VC\lib\x64\SDL2.dll",
	[string]$Out = "$PSScriptRoot\stage"
)

$ErrorActionPreference = "Stop"

function Copy-Tree( [string]$from, [string]$to )
{
	# no debug symbols, import libraries or static libraries in a player's install
	Get-ChildItem $from -Recurse -File | Where-Object { $_.Extension -notin '.pdb', '.lib', '.exp', '.ilk' } | ForEach-Object {
		$dest = Join-Path $to $_.FullName.Substring(( Resolve-Path $from ).Path.Length).TrimStart('\')
		New-Item -ItemType Directory -Force ( Split-Path $dest ) | Out-Null
		Copy-Item $_.FullName $dest
	}
}

if( Test-Path $Out ) { [IO.Directory]::Delete(( Resolve-Path $Out ).Path, $true ) }
New-Item -ItemType Directory -Force $Out | Out-Null

# the engine: executable, engine, renderers, filesystem, menus, VGUI support;
# not the tools (mdldec, ktx2enc) nor the folders of test runs
Get-ChildItem $Engine -File | Where-Object { $_.Extension -in '.exe', '.dll' -and $_.BaseName -notin 'mdldec', 'ktx2enc' } |
	Copy-Item -Destination $Out
if( Test-Path "$Engine\valve" ) { Copy-Tree "$Engine\valve" "$Out\valve" } # extras.pk3 of the engine

Copy-Item $Sdl2 $Out

# the MSVC runtime next to the executable (app-local deployment): a fresh
# Windows 10 has the UCRT but not these
$redist = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio", "$env:ProgramFiles\Microsoft Visual Studio" -Recurse -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -match '\\Redist\\MSVC\\[^\\]+\\x64\\' -and $_.FullName -notmatch 'debug|onecore' } | Sort-Object FullName | Select-Object -Last 1
if( -not $redist ) { throw "no MSVC x64 runtime found (Visual Studio redistributable folder)" }
foreach( $dll in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll' ) { Copy-Item ( Join-Path $redist.FullName $dll ) $Out }

Copy-Tree $Hlsdk $Out
Copy-Tree $Cs16 $Out

# what the installer checks for
foreach( $f in 'xash3d.exe', 'xash.dll', 'ref_vk.dll', 'ref_gl.dll', 'filesystem_stdio.dll', 'SDL2.dll',
	'valve\cl_dlls\client_amd64.dll', 'valve\dlls\hl_amd64.dll',
	'cstrike\cl_dlls\client_amd64.dll', 'cstrike\cl_dlls\menu_amd64.dll', 'cstrike\dlls\mp_amd64.dll', 'cstrike\dlls\yapb_amd64.dll', 'cstrike\extras.pk3' )
{
	if( -not ( Test-Path "$Out\$f" )) { throw "missing in the staging folder: $f" }
}

$size = ( Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum ).Sum
"staged {0} files, {1:N1} MB in {2}" -f ( Get-ChildItem $Out -Recurse -File ).Count, ( $size / 1MB ), ( Resolve-Path $Out )
