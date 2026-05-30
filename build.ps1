<#
.SYNOPSIS
    Arisa Engine build script
.DESCRIPTION
    Builds ars.exe using CMake + MSVC
.PARAMETER Config
    Build configuration: Debug (default) or Release
.PARAMETER Clean
    Clean build directory before building
.EXAMPLE
    .\build.ps1
    .\build.ps1 -Config Release -Clean
#>

param(
    [ValidateSet("Debug", "Release")]
    [string] = "Debug",
    [switch]
)

Continue = "Stop"

D:\Project\arisa = 
 = Join-Path D:\Project\arisa "build"

Write-Host ""
Write-Host "  +===================================+" -ForegroundColor Cyan
Write-Host "  |  Arisa Engine Build Script         |" -ForegroundColor Cyan
Write-Host "  +===================================+" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Config:  " -ForegroundColor White
Write-Host "  Build:   " -ForegroundColor White
Write-Host ""

if () {
    Write-Host "  Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force  -ErrorAction SilentlyContinue
}

# Detect VS version
 = "Visual Studio 18 2026"
if (Get-Command "vswhere.exe" -ErrorAction SilentlyContinue) {
     = & vswhere.exe -latest -property catalog_productLineVersion 2>
    if ( -eq "2022") {  = "Visual Studio 17 2022" }
    elseif ( -eq "2019") {  = "Visual Studio 16 2019" }
}

Write-Host "  Configuring ()..." -ForegroundColor Yellow
cmake -B  -G  -A x64
if (0 -ne 0) { Write-Host "  CMake configure failed!" -ForegroundColor Red; exit 1 }

Write-Host "  Building ()..." -ForegroundColor Yellow
cmake --build  --config 
if (0 -ne 0) { Write-Host "  Build failed!" -ForegroundColor Red; exit 1 }

 = Join-Path  "/ars.exe"
if (Test-Path ) {
     = [math]::Round((Get-Item ).Length / 1MB, 2)
    Write-Host ""
    Write-Host "  [OK]  ( MB)" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "  [ERR] ars.exe not found at " -ForegroundColor Red
    exit 1
}