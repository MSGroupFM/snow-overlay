$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$Build = Join-Path $Root 'build\windows'
New-Item -ItemType Directory -Path $Build -Force | Out-Null
Push-Location $PSScriptRoot
try {
    $Resource = Join-Path $Build 'SnowOverlay.res'
    & rc.exe /nologo "/fo$Resource" SnowOverlay.rc
    if ($LASTEXITCODE -ne 0) { throw 'rc.exe failed' }
    $Exe = Join-Path $Build 'SnowOverlay.exe'
    $Core = Join-Path $Root 'core'
    $Compiler = @('/nologo', '/std:c17', '/utf-8', '/W4', '/O2', '/MT', "/I$Core",
                  "/Fo$Build/", "/Fe$Exe", 'main.c', (Join-Path $Core 'snow.c'), $Resource)
    $Linker = @('/link', '/SUBSYSTEM:WINDOWS', '/MANIFEST:NO', '/DYNAMICBASE', '/NXCOMPAT',
                '/HIGHENTROPYVA', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'dwmapi.lib')
    & cl.exe @Compiler @Linker
    if ($LASTEXITCODE -ne 0) { throw 'cl.exe failed' }
    Write-Output $Exe
} finally {
    Pop-Location
}
