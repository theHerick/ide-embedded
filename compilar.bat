@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo [1/3] Iniciando o Build do Projeto...
echo ==========================================
set PATH=C:\msys64\ucrt64\bin;%PATH%

if not exist build (
    echo Pasta 'build' nao encontrada. Configurando CMake...
    cmake -S . -B build -G Ninja
    if %ERRORLEVEL% neq 0 (
        echo [ERRO] Falha ao configurar o CMake.
        exit /b %ERRORLEVEL%
    )
)

cmake --build build
if %ERRORLEVEL% neq 0 (
    echo [ERRO] O build falhou! Nao sera feito o commit ou push.
    exit /b %ERRORLEVEL%
)

if exist build\apps\ide\ide-embedded.exe (
    if not exist bin mkdir bin
    copy /Y build\apps\ide\ide-embedded.exe bin\ide-embedded.exe >nul
    echo Executavel gerado em: bin\ide-embedded.exe
) else (
    echo [ERRO] O build concluiu, mas o executavel nao foi encontrado em build\apps\ide\ide-embedded.exe
    exit /b 1
)

echo ==========================================
echo [2/3] Build concluido com sucesso!
echo Preparando commit automatico...
echo ==========================================

for /f "usebackq delims=" %%b in (`git branch --show-current`) do set CURRENT_BRANCH=%%b
if not "%CURRENT_BRANCH%"=="main" (
    echo [AVISO] Branch atual: %CURRENT_BRANCH%.
    echo [AVISO] Commit e push automaticos foram ignorados para evitar falha fora da branch main.
    echo ==========================================
    echo [3/3] Processo finalizado com sucesso!
    echo ==========================================
    exit /b 0
)

:: Verifica se ha alteracoes para commitar
git status --porcelain | findstr /R "^" >nul
if %ERRORLEVEL% neq 0 (
    echo Nenhuma alteracao de codigo encontrada para commitar.
    echo ==========================================
    echo [3/3] Processo finalizado com sucesso!
    echo ==========================================
    exit /b 0
)

echo Salvando alteracoes com Git...
git add .

:: Obtem o proximo ID incremental para o commit baseado no historico do Git
for /f "usebackq tokens=*" %%i in (`powershell -NoProfile -Command "$commits = git log --grep='^ID:' --pretty=format:'%%s'; $max = 0; foreach ($c in $commits) { if ($c -match '^ID:\s*(\d+)') { $num = [int]$Matches[1]; if ($num -gt $max) { $max = $num } } }; $next = $max + 1; Write-Output $next"`) do set NEXT_ID=%%i

echo Novo ID de Commit: ID:%NEXT_ID%
git commit -m "ID:%NEXT_ID%"
if %ERRORLEVEL% neq 0 (
    echo [AVISO] Falha ao criar o commit.
    exit /b 0
)

echo ==========================================
echo [3/3] Enviando alteracoes para o GitHub (Push)...
echo ==========================================
git push origin main
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha ao dar push para o GitHub. Verifique sua conexao ou permissao.
    exit /b %ERRORLEVEL%
)

echo ==========================================
echo Concluido! Build, Commit e Push realizados com sucesso!
echo ==========================================
:: ID Auto-Increment System Verified
