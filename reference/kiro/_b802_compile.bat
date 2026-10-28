@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\z\code\OpenMonkey\tsfp-openmonkey\tsfp-decomp"
cl /nologo /c /EHsc /std:c++17 /I include src\game\core\menu_loadout_screen2.cpp > _b802_out.log 2>&1
echo EXIT=%ERRORLEVEL% >> _b802_out.log
