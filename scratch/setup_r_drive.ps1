$cfgDir = 'c:\Users\heric\Desktop\IDE-embedded\cppcheck_cfg\winlibs64ucrt_stage\inst_cppcheck-2.14.0\share\Cppcheck\cfg'
if (!(Test-Path $cfgDir)) {
    New-Item -ItemType Directory -Force -Path $cfgDir
}
Write-Output "Baixando std.cfg..."
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/danmar/cppcheck/main/cfg/std.cfg" -OutFile "$cfgDir\std.cfg"
Write-Output "Baixando posix.cfg..."
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/danmar/cppcheck/main/cfg/posix.cfg" -OutFile "$cfgDir\posix.cfg"
Write-Output "Baixando windows.cfg..."
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/danmar/cppcheck/main/cfg/windows.cfg" -OutFile "$cfgDir\windows.cfg"

Write-Output "Mapeando unidade R:..."
cmd /c "subst R: /D 2>nul"
subst R: 'c:\Users\heric\Desktop\IDE-embedded\cppcheck_cfg'
Write-Output "Unidade R: mapeada com sucesso!"
