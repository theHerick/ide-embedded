@echo off
:: Adiciona o caminho do MSYS2/Qt ao PATH temporário para que o executável encontre as DLLs necessárias
set PATH=C:\msys64\ucrt64\bin;%PATH%

:: Inicia a IDE Embedded
echo Iniciando a IDE Embedded...
start "" "%~dp0build\apps\ide\ide-embedded.exe"
