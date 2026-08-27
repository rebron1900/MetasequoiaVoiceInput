param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build"
$packageRoot = Join-Path $projectRoot "package"
$packageDir = Join-Path $packageRoot "MetasequoiaVoiceInput"
$archivePath = Join-Path $packageRoot "MetasequoiaVoiceInput-windows-x64.zip"

cmake -S $projectRoot -B $buildDir -DCMAKE_BUILD_TYPE=$Configuration -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build $buildDir --config $Configuration --parallel

Remove-Item -Recurse -Force $packageDir -ErrorAction SilentlyContinue
Remove-Item -Force $archivePath -ErrorAction SilentlyContinue
cmake --install $buildDir --config $Configuration --prefix $packageDir
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $archivePath

Write-Host "Release archive created: $archivePath"
