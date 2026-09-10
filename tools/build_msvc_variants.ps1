param(
    [string]$VsInstanceId   = "fdfdfdfd",
    [string]$VsPath         = "C:\Microsoft Visual Studio\2022\Community"
)
$ErrorActionPreference = "Stop"

$Root    = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Include = @($Root, (Join-Path $Root "Reflection"))
$DllSrc  = Join-Path $Root "dlls"
$Variant = Join-Path $Root "variants"
$Tag     = "msvc_x64"

$devshell = Join-Path $VsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path $devshell)) { throw "VsDevShell not found: $devshell" }
Import-Module $devshell
Enter-VsDevShell $VsInstanceId -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' 2>&1 | Out-Null
if (-not $env:VCINSTALLDIR) { throw "VC environment not obtained after Enter-VsDevShell (VCINSTALLDIR is empty)" }

$env:INCLUDE = (($env:INCLUDE -split ';') | Where-Object { $_ -and $_ -notmatch 'msys2|mingw|ucrt64' }) -join ';'
$env:LIB     = (($env:LIB     -split ';') | Where-Object { $_ -and $_ -notmatch 'msys2|mingw|ucrt64' }) -join ';'
$env:PATH    = (($env:PATH    -split ';') | Where-Object { $_ -and $_ -notmatch 'msys2-data' }) -join ';'
Write-Host "== Entered MSVC: $env:VCINSTALLDIR =="
Write-Host "   INCLUDE contains msys2? $($env:INCLUDE -match 'msys2')"

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) { throw "cl.exe not found in PATH" }
$clPath = $cl.Path
Write-Host "== cl: $clPath =="

function Build-Variant([string]$dllName, [string]$srcName) {
    $outDir = Join-Path (Join-Path $Variant $Tag) $dllName
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $src = Join-Path $DllSrc "$srcName.cpp"
    $obj = Join-Path $outDir "$srcName.obj"
    $dll = Join-Path $outDir "$dllName.dll"
    Write-Host "== [$Tag] Compiling $srcName -> $dll =="
    $CompileArgs = @(
        "/nologo"
        "/TP"
        "/std:c++20"
        "/MD"
        "/utf-8"
        "/EHsc"
        "/O2"
        "/permissive-"
        "/D_AMD64_"
    )

    $CompileArgs += $Include | ForEach-Object {
        "/I$_"
    }

    $CompileArgs += @(
        "/c"
        $src
        "/Fo$obj"
    )

    & $clPath $CompileArgs
    if ($LASTEXITCODE -ne 0) { throw "[$srcName] Compilation failed" }
    & $clPath /nologo $obj /Fe"$dll" /LD 2>&1 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) { throw "[$srcName] Linking failed" }
    Remove-Item -Force (Join-Path $outDir "$srcName.obj"),(Join-Path $outDir "$srcName.exp"),(Join-Path $outDir "$srcName.lib") -ErrorAction SilentlyContinue
    Write-Host "    Generated: $dll"

    $stage = Join-Path $Root "build\plugins\variants"
    $dst = Join-Path (Join-Path $stage $Tag) $dllName
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item $dll (Join-Path $dst "$dllName.dll") -Force
    Write-Host "    Copied to: $dst"
}

Build-Variant "version_dll"  "version_dll"
Build-Variant "resource_dll" "resource_dll"

Write-Host "== MSVC variant build complete =="