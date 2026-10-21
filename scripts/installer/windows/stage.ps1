<#
stage.ps1 - builds the whole project and gathers the files of the Windows
installer (story 052)

From this repository: the 64-bit engine (client, both renderers, menus), the
dedicated server, Half-Life's game libraries (halflife-15) and Counter-
Strike's (cstrike-17: client, menus, server ReGameDLL_CS, bots), with SDL2
and the MSVC runtime, laid out as the installed game in stage\. The
installer script (xash3d.iss) packs it. The game data isn't staged: the
installer copies it from the player's Steam folder.

  -Build    where the builds go (default C:\xash-dev\build)
  -NoBuild  only gather what the last builds made
  -Sdl2     SDL2.dll, x64
  -Out      the staging folder, emptied first

Needs Visual Studio 2026 Build Tools (C++, CMake), Python, the Vulkan SDK
(glslc) and Rust (the text menu), as the builds do.
#>
param(
	[string]$Build = "C:\xash-dev\build",
	[switch]$NoBuild,
	[string]$Sdl2 = "$PSScriptRoot\..\..\..\SDL2_VC\lib\x64\SDL2.dll",
	[string]$Out = "$PSScriptRoot\stage"
)

$ErrorActionPreference = "Stop"
$Repo = ( Resolve-Path "$PSScriptRoot\..\..\.." ).Path

function Find-CMake
{
	$c = Get-Command cmake -ErrorAction SilentlyContinue
	if( $c ) { return $c.Source }
	$c = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio", "$env:ProgramFiles\Microsoft Visual Studio" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match 'CMake\\bin\\cmake.exe$' } | Select-Object -First 1
	if( -not $c ) { throw "cmake not found" }
	return $c.FullName
}

function Invoke-Checked( [string]$what, [scriptblock]$cmd )
{
	Write-Host "== $what"
	& $cmd
	if( $LASTEXITCODE ) { throw "$what failed ($LASTEXITCODE)" }
}

function Copy-Tree( [string]$from, [string]$to )
{
	# no debug symbols, import libraries or static libraries in a player's install
	Get-ChildItem $from -Recurse -File | Where-Object { $_.Extension -notin '.pdb', '.lib', '.exp', '.ilk' } | ForEach-Object {
		$dest = Join-Path $to $_.FullName.Substring(( Resolve-Path $from ).Path.Length).TrimStart('\')
		New-Item -ItemType Directory -Force ( Split-Path $dest ) | Out-Null
		Copy-Item $_.FullName $dest
	}
}

$client = "$Build\engine-win64"
$dedicated = "$Build\dedicated-win64"
$games = "$Build\games-win64"

if( -not $NoBuild )
{
	$vulkan = Get-ChildItem "C:\VulkanSDK" -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
	if( $vulkan ) { $env:PATH = "$( $vulkan.FullName )\Bin;$env:PATH" }
	$cmake = Find-CMake

	Push-Location $Repo
	try
	{
		# the engine, client: its own lock file and build directory
		$env:WAFLOCK = ".lock-waf-x64"
		if( -not ( Test-Path "$Repo\build-x64" )) {
			Invoke-Checked "configure the engine" { .\waf.bat configure -o build-x64 -s SDL2_VC -T release -8 --msvc_targets=x64 --enable-tui --enable-vk }
		}
		Invoke-Checked "build the engine" { .\waf.bat build install "--destdir=$client" }

		# the dedicated server
		$env:WAFLOCK = ".lock-waf-dedicated-x64"
		if( -not ( Test-Path "$Repo\build-dedicated-x64" )) {
			Invoke-Checked "configure the dedicated server" { .\waf.bat configure -o build-dedicated-x64 -T release -8 --msvc_targets=x64 -d }
		}
		Invoke-Checked "build the dedicated server" { .\waf.bat build install "--destdir=$dedicated" }
		$env:WAFLOCK = $null
	}
	finally { Pop-Location }

	# the games, installed under one prefix: valve\..., cstrike\...
	foreach( $g in @( @( "halflife-15", "-D64BIT=ON" ), @( "cstrike-17", "-DENABLE_YY_THUNKS=OFF" )))
	{
		$b = "$Build\$( $g[0] )-win64"
		Invoke-Checked "configure $( $g[0] )" { & $cmake -S "$Repo\$( $g[0] )" -B $b -G "Visual Studio 18 2026" -A x64 $g[1] }
		Invoke-Checked "build $( $g[0] )" { & $cmake --build $b --config Release -- /m /v:minimal }
		Invoke-Checked "install $( $g[0] )" { & $cmake --install $b --config Release --prefix $games }
	}
}

if( Test-Path $Out ) { [IO.Directory]::Delete(( Resolve-Path $Out ).Path, $true ) }
New-Item -ItemType Directory -Force $Out | Out-Null

# the engine: executable, engine, renderers, filesystem, menus, VGUI support;
# not the tools (mdldec, ktx2enc)
Get-ChildItem $client -File | Where-Object { $_.Extension -in '.exe', '.dll' -and $_.BaseName -notin 'mdldec', 'ktx2enc' } |
	Copy-Item -Destination $Out
if( Test-Path "$client\valve" ) { Copy-Tree "$client\valve" "$Out\valve" } # the engine's extras.pk3

# the dedicated server next to it: same filesystem library, its own executable
Copy-Item "$dedicated\xash.exe" "$Out\xash-dedicated.exe"

Copy-Item $Sdl2 $Out

# the MSVC runtime next to the executables (app-local deployment): a fresh
# Windows 10 has the UCRT but not these
$redist = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio", "$env:ProgramFiles\Microsoft Visual Studio" -Recurse -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -match '\\Redist\\MSVC\\[^\\]+\\x64\\' -and $_.FullName -notmatch 'debug|onecore' } | Sort-Object FullName | Select-Object -Last 1
if( -not $redist ) { throw "no MSVC x64 runtime found (Visual Studio redistributable folder)" }
foreach( $dll in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll' ) { Copy-Item ( Join-Path $redist.FullName $dll ) $Out }

Copy-Tree $games $Out

# the dedicated servers' settings
Copy-Item "$PSScriptRoot\server-cstrike.cfg" "$Out\cstrike\server.cfg"
Copy-Item "$PSScriptRoot\server-valve.cfg" "$Out\valve\server.cfg"

# what the installer checks for
foreach( $f in 'xash3d.exe', 'xash.dll', 'xash-dedicated.exe', 'ref_vk.dll', 'ref_gl.dll', 'filesystem_stdio.dll', 'SDL2.dll',
	'valve\cl_dlls\client_amd64.dll', 'valve\dlls\hl_amd64.dll', 'valve\server.cfg',
	'cstrike\cl_dlls\client_amd64.dll', 'cstrike\cl_dlls\menu_amd64.dll', 'cstrike\dlls\mp_amd64.dll', 'cstrike\dlls\yapb_amd64.dll',
	'cstrike\extras.pk3', 'cstrike\server.cfg', 'tools\patch_dust2.exe' )
{
	if( -not ( Test-Path "$Out\$f" )) { throw "missing in the staging folder: $f" }
}

$size = ( Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum ).Sum
"staged {0} files, {1:N1} MB in {2}" -f ( Get-ChildItem $Out -Recurse -File ).Count, ( $size / 1MB ), ( Resolve-Path $Out )
