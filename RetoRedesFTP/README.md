# Sistema de Servidor y Cliente FTP en C para Windows

Sistema completo de comunicación cliente-servidor FTP implementado en C con soporte para modo pasivo (PASV) según RFC 959.

##  Características

### Servidor FTP
- ✅ Implementación completa del protocolo FTP (RFC 959)
- ✅ Modo PASV para transferencia de archivos
- ✅ Comandos soportados: USER, PASS, PWD, CWD, LIST, RETR, STOR, QUIT
- ✅ Autenticación desde archivo de configuración
- ✅ Logs detallados con timestamp, IP, comando, estado, duración y tamaño
- ✅ Manejo multi-cliente con hilos (threads)

### Cliente FTP
- ✅ Interfaz interactiva por consola
- ✅ Soporte completo para modo PASV
- ✅ Comandos: LIST, RETR (descargar), STOR (subir)
- ✅ Validación de operaciones
- ✅ Estadísticas de transferencia (velocidad, tiempo, tamaño)

### Scripts PowerShell
- ✅ Gestión automática del servidor (inicio/parada)
- ✅ Configuración de firewall para puertos necesarios
- ✅ Resumen de logs con métricas
- ✅ Pruebas automatizadas

## Estructura del Proyecto