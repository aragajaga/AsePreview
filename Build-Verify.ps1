[CmdletBinding()]
param([string]$IllustratorPresets = 'C:\Program Files\Adobe\Adobe Illustrator 2026\Presets')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (-not $install) { throw 'Visual Studio/MSBuild not found.' }
$msbuild = Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logDir = Join-Path $PSScriptRoot "_maintenance\verification\$stamp"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
foreach ($platform in @('x64','Win32')) {
    foreach ($configuration in @('Debug','Release')) {
        foreach ($project in @('AsePreview.vcxproj','tests\AsePreviewTests.vcxproj')) {
            $projectPath = Join-Path $PSScriptRoot $project
            & $msbuild $projectPath /t:Build "/p:Configuration=$configuration" "/p:Platform=$platform" /m /v:minimal /nologo
            if ($LASTEXITCODE -ne 0) { throw "Build failed: $project $platform $configuration" }
        }
        $output = if ($platform -eq 'x64') { "x64\$configuration" } else { $configuration }
        $dll = Join-Path $PSScriptRoot "$output\AsePreview.dll"
        $test = Join-Path $PSScriptRoot "tests\_build\$platform\$configuration\AsePreviewTests.exe"
        $arguments = @($dll)
        if (Test-Path -LiteralPath $IllustratorPresets) { $arguments += $IllustratorPresets }
        & $test @arguments | Tee-Object -FilePath (Join-Path $logDir "$platform-$configuration.txt")
        if ($LASTEXITCODE -ne 0) { throw "Tests failed: $platform $configuration" }
    }
}
Write-Output "All four configurations passed. Logs: $logDir"
