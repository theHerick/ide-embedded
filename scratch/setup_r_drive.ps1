$repoRoot = Split-Path $PSScriptRoot -Parent
$cppcheckRoot = Join-Path $repoRoot 'cppcheck_cfg'
$cfgDir = Join-Path $cppcheckRoot 'winlibs64ucrt_stage\inst_cppcheck-2.14.0\share\Cppcheck\cfg'

if (!(Test-Path $cfgDir)) {
    New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
}

if (!(Test-Path (Join-Path $cfgDir 'std.cfg'))) {
    throw "Missing Cppcheck cfg files under $cfgDir"
}

Write-Output "Mapeando unidade R:..."
subst R: $cppcheckRoot
Write-Output "Unidade R: mapeada com sucesso!"
