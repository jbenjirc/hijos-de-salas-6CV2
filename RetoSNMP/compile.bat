@echo off
REM compile.bat - Script de compilación para SNMP (Windows)

echo ╔════════════════════════════════════════════════════════════╗
echo ║      Compilador SNMP - Proyecto Redes ESCOM              ║
echo ╚════════════════════════════════════════════════════════════╝
echo.

REM Verificar MinGW
where gcc >nul 2>&1
if %errorlevel% equ 0 (
    echo [✓] MinGW GCC encontrado
    echo.
    echo Compilando con MinGW...
    echo ─────────────────────────────────────────────────────────────
    
    echo Compilando snmp_agent.c...
    gcc -Wall -o snmp_agent.exe snmp_agent.c -lws2_32
    if %errorlevel% equ 0 (
        echo [✓] snmp_agent.exe compilado correctamente
    ) else (
        echo [✗] Error compilando snmp_agent.c
    )
    
    echo.
    echo Compilando snmp_client.c...
    gcc -Wall -o snmp_client.exe snmp_client.c -lws2_32
    if %errorlevel% equ 0 (
        echo [✓] snmp_client.exe compilado correctamente
    ) else (
        echo [✗] Error compilando snmp_client.c
    )
    
    goto :end
)

REM Verificar MSVC
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo [✓] MSVC encontrado
    echo.
    echo Compilando con MSVC...
    echo ─────────────────────────────────────────────────────────────
    
    echo Compilando snmp_agent.c...
    cl /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:snmp_agent.exe snmp_agent.c ws2_32.lib
    if %errorlevel% equ 0 (
        echo [✓] snmp_agent.exe compilado correctamente
    ) else (
        echo [✗] Error compilando snmp_agent.c
    )
    
    echo.
    echo Compilando snmp_client.c...
    cl /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:snmp_client.exe snmp_client.c ws2_32.lib
    if %errorlevel% equ 0 (
        echo [✓] snmp_client.exe compilado correctamente
    ) else (
        echo [✗] Error compilando snmp_client.c
    )
    
    REM Limpiar archivos temporales MSVC
    del *.obj >nul 2>&1
    
    goto :end
)

echo [✗] No se encontró ningún compilador C (MinGW o MSVC)
echo.
echo Instalar MinGW desde: https://sourceforge.net/projects/mingw/
echo O usar Visual Studio Developer Command Prompt para MSVC
goto :end

:end
echo.
echo ═════════════════════════════════════════════════════════════
echo Compilación finalizada
echo.

if exist snmp_agent.exe (
    echo Para ejecutar el agente:
    echo   snmp_agent.exe
    echo.
)

if exist snmp_client.exe (
    echo Para ejecutar el cliente:
    echo   snmp_client.exe
    echo.
)

echo NOTA: El agente requiere permisos de Administrador para puerto 161
echo ═════════════════════════════════════════════════════════════
echo.

pause
