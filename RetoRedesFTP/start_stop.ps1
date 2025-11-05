# AÑADIDO: Forzar codificación UTF-8 para acentos
$OutputEncoding = [System.Text.Encoding]::UTF8

# start_stop.ps1 - Scripts de gestión del servidor FTP

# ==================== FUNCIONES ====================

function Start-FTPServer {
    Write-Host "=== Iniciando Servidor FTP ===" -ForegroundColor Cyan
    
    # Verificar si el servidor ya está ejecutándose
    $process = Get-Process -Name "ftp_server" -ErrorAction SilentlyContinue
    if ($process) {
        Write-Host "✗ El servidor FTP ya está ejecutándose (PID: $($process.Id))" -ForegroundColor Yellow
        return
    }
    
    # Verificar que el ejecutable existe
    if (-not (Test-Path ".\ftp_server.exe")) {
        Write-Host "✗ Error: ftp_server.exe no encontrado" -ForegroundColor Red
        Write-Host "  Compilar con: gcc ftp_server.c -o ftp_server.exe -lws2_32" -ForegroundColor Yellow
        return
    }
    
    # Crear directorios necesarios
    $directories = @("logs", "config", "ftp", "http", "smtp", "snmp", "rmi", "common", "tests", "docs")
    foreach ($dir in $directories) {
        if (-not (Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir | Out-Null
            Write-Host "✓ Directorio creado: $dir" -ForegroundColor Green
        }
    }
    
    # Crear archivo de usuarios si no existe
    if (-not (Test-Path "config\users.txt")) {
        @"
admin:admin123
user:password
test:test123
"@ | Out-File -FilePath "config\users.txt" -Encoding ASCII
        Write-Host "✓ Archivo de usuarios creado: config\users.txt" -ForegroundColor Green
    }
    
    # Habilitar puertos en el firewall
    Write-Host "`nConfigurando firewall..." -ForegroundColor Yellow
    Enable-FirewallRules
    
    # Iniciar servidor en segundo plano
    Write-Host "`nIniciando servidor FTP..." -ForegroundColor Yellow
    Start-Process -FilePath ".\ftp_server.exe" -WindowStyle Minimized
    
    Start-Sleep -Seconds 2
    
    $process = Get-Process -Name "ftp_server" -ErrorAction SilentlyContinue
    if ($process) {
        Write-Host "✓ Servidor FTP iniciado correctamente (PID: $($process.Id))" -ForegroundColor Green
        Write-Host "  Puerto: 21" -ForegroundColor Cyan
        Write-Host "  Log: logs\ftp_server.log" -ForegroundColor Cyan
    } else {
        Write-Host "✗ Error al iniciar el servidor" -ForegroundColor Red
    }
}

function Stop-FTPServer {
    Write-Host "=== Deteniendo Servidor FTP ===" -ForegroundColor Cyan
    
    $process = Get-Process -Name "ftp_server" -ErrorAction SilentlyContinue
    if (-not $process) {
        Write-Host "✗ El servidor FTP no está ejecutándose" -ForegroundColor Yellow
        return
    }
    
    Write-Host "Deteniendo servidor (PID: $($process.Id))..." -ForegroundColor Yellow
    Stop-Process -Name "ftp_server" -Force
    Start-Sleep -Seconds 1
    
    $process = Get-Process -Name "ftp_server" -ErrorAction SilentlyContinue
    if (-not $process) {
        Write-Host "✓ Servidor FTP detenido correctamente" -ForegroundColor Green
    } else {
        Write-Host "✗ Error al detener el servidor" -ForegroundColor Red
    }
}

function Get-FTPServerStatus {
    Write-Host "=== Estado del Servidor FTP ===" -ForegroundColor Cyan
    
    $process = Get-Process -Name "ftp_server" -ErrorAction SilentlyContinue
    if ($process) {
        Write-Host "Estado: EJECUTÁNDOSE" -ForegroundColor Green
        Write-Host "PID: $($process.Id)" -ForegroundColor Cyan
        Write-Host "Memoria: $([math]::Round($process.WorkingSet / 1MB, 2)) MB" -ForegroundColor Cyan
        Write-Host "Tiempo activo: $($process.StartTime)" -ForegroundColor Cyan
        
        # Verificar puerto
        $connection = Get-NetTCPConnection -LocalPort 21 -State Listen -ErrorAction SilentlyContinue
        if ($connection) {
            Write-Host "Puerto 21: ESCUCHANDO" -ForegroundColor Green
        } else {
            Write-Host "Puerto 21: NO DISPONIBLE" -ForegroundColor Red
        }
    } else {
        Write-Host "Estado: DETENIDO" -ForegroundColor Red
    }
}

function Enable-FirewallRules {
    $ports = @(21, 23, 25, 80, 161, 162, 8080, 1099)
    
    foreach ($port in $ports) {
        $ruleName = "FTP_Server_Port_$port"
        
        # Eliminar regla existente si existe
        $existingRule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
        if ($existingRule) {
            Remove-NetFirewallRule -DisplayName $ruleName | Out-Null
        }
        
        # Crear nueva regla
        try {
            New-NetFirewallRule -DisplayName $ruleName `
                                -Direction Inbound `
                                -Protocol TCP `
                                -LocalPort $port `
                                -Action Allow `
                                -Enabled True | Out-Null
            Write-Host "✓ Puerto $port habilitado en firewall" -ForegroundColor Green
        } catch {
            Write-Host "✗ Error habilitando puerto $port (requiere permisos de administrador)" -ForegroundColor Yellow
        }
    }
}

function Disable-FirewallRules {
    Write-Host "=== Deshabilitando Reglas de Firewall ===" -ForegroundColor Cyan
    
    $ports = @(21, 23, 25, 80, 161, 162, 8080, 1099)
    
    foreach ($port in $ports) {
        $ruleName = "FTP_Server_Port_$port"
        
        $rule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
        if ($rule) {
            Remove-NetFirewallRule -DisplayName $ruleName | Out-Null
            Write-Host "✓ Regla eliminada para puerto $port" -ForegroundColor Green
        }
    }
}

function Show-LogSummary {
    param (
        [int]$LastMinutes = 60
    )
    
    Write-Host "=== Resumen de Logs (últimos $LastMinutes minutos) ===" -ForegroundColor Cyan
    
    if (-not (Test-Path "logs\ftp_server.log")) {
        Write-Host "✗ Archivo de log no encontrado" -ForegroundColor Red
        return
    }
    
    $cutoffTime = (Get-Date).AddMinutes(-$LastMinutes)
    $logs = Get-Content "logs\ftp_server.log" | Where-Object { $_ -match "^\[" }
    
    $stats = @{
        USER = 0
        PASS = 0
        LIST = 0
        RETR = 0
        STOR = 0
        CWD = 0
        PWD = 0
        QUIT = 0
        TotalSize = 0
        TotalDuration = 0
        Errors = 0
    }
    
    foreach ($line in $logs) {
        if ($line -match "CMD=(\w+)") {
            $cmd = $matches[1]
            if ($stats.ContainsKey($cmd)) {
                $stats[$cmd]++
            }
        }
        
        if ($line -match "SIZE=(\d+)") {
            $stats.TotalSize += [int]$matches[1]
        }
        
        if ($line -match "DURATION=(\d+)") {
            $stats.TotalDuration += [int]$matches[1]
        }
        
        if ($line -match "STATUS=(530|550|425)") {
            $stats.Errors++
        }
    }
    
    Write-Host "`nComandos ejecutados:" -ForegroundColor Yellow
    foreach ($key in $stats.Keys | Where-Object { $_ -match "^[A-Z]{3,4}$" }) {
        if ($stats[$key] -gt 0) {
            Write-Host "  $key`: $($stats[$key])" -ForegroundColor Cyan
        }
    }
    
    Write-Host "`nEstadísticas:" -ForegroundColor Yellow
    Write-Host "  Datos transferidos: $([math]::Round($stats.TotalSize / 1MB, 2)) MB" -ForegroundColor Cyan
    Write-Host "  Tiempo total: $([math]::Round($stats.TotalDuration / 1000, 2)) segundos" -ForegroundColor Cyan
    Write-Host "  Errores: $($stats.Errors)" -ForegroundColor $(if ($stats.Errors -gt 0) { "Red" } else { "Green" })
}

function Test-FTPCommands {
    Write-Host "=== Pruebas Automatizadas FTP ===" -ForegroundColor Cyan
    
    # Crear archivo de prueba
    $testFile = "test_upload.txt"
    "Este es un archivo de prueba para FTP" | Out-File -FilePath $testFile -Encoding ASCII
    
    Write-Host "`n1. Iniciando servidor..." -ForegroundColor Yellow
    Start-FTPServer
    Start-Sleep -Seconds 3
    
    Write-Host "`n2. Probando conexión y autenticación..." -ForegroundColor Yellow
    Write-Host "  (Ejecutar cliente manualmente para pruebas interactivas)" -ForegroundColor Cyan
    Write-Host "  Comando: .\ftp_client.exe" -ForegroundColor Cyan
    
    Write-Host "`n3. Comandos a probar:" -ForegroundColor Yellow
    Write-Host "  ✓ USER/PASS - Autenticación" -ForegroundColor Cyan
    Write-Host "  ✓ PWD - Directorio actual" -ForegroundColor Cyan
    Write-Host "  ✓ LIST - Listar archivos" -ForegroundColor Cyan
    Write-Host "  ✓ STOR - Subir archivo" -ForegroundColor Cyan
    Write-Host "  ✓ RETR - Descargar archivo" -ForegroundColor Cyan
    Write-Host "  ✓ QUIT - Cerrar conexión" -ForegroundColor Cyan
    
    Write-Host "`nArchivo de prueba creado: $testFile" -ForegroundColor Green
}

function Show-Menu {
    Write-Host "`n╔════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║   GESTOR DE SERVIDOR FTP - WINDOWS    ║" -ForegroundColor Cyan
    Write-Host "╚════════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "1. Iniciar servidor FTP" -ForegroundColor White
    Write-Host "2. Detener servidor FTP" -ForegroundColor White
    Write-Host "3. Estado del servidor" -ForegroundColor White
    Write-Host "4. Habilitar puertos en firewall" -ForegroundColor White
    Write-Host "5. Deshabilitar puertos en firewall" -ForegroundColor White
    Write-Host "6. Resumen de logs (última hora)" -ForegroundColor White
    Write-Host "7. Ejecutar pruebas automatizadas" -ForegroundColor White
    Write-Host "8. Salir" -ForegroundColor White
    Write-Host ""
}

# ==================== MENÚ PRINCIPAL ====================

while ($true) {
    Show-Menu
    $option = Read-Host "Seleccione una opción"
    
    switch ($option) {
        "1" { Start-FTPServer }
        "2" { Stop-FTPServer }
        "3" { Get-FTPServerStatus }
        "4" { Enable-FirewallRules }
        "5" { Disable-FirewallRules }
        "6" { Show-LogSummary -LastMinutes 60 }
        "7" { Test-FTPCommands }
        "8" { 
            Write-Host "`nSaliendo..." -ForegroundColor Yellow
            exit 
        }
        default { 
            Write-Host "✗ Opción inválida" -ForegroundColor Red 
        }
    }
    
    Write-Host "`nPresione ENTER para continuar..."
    Read-Host
    Clear-Host
}