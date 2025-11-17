# Reto 4: SNMP v1/v2c

Agente SNMP educativo que responde peticiones GetRequest en UDP puerto 161 y envía traps a puerto 162.

## Características

- **Agente UDP 161**: Responde a GetRequest/GetNextRequest
- **Community**: `public`
- **OIDs de sistema**: sysDescr, sysUpTime, sysName, sysContact, sysLocation
- **OIDs personalizados**: Métricas de FTP, HTTP, SMTP, CPU, Memoria
- **Traps automáticos**: Envío a UDP 162 cuando CPU/Memoria > 80% o Errores > 10

## Compilación

```bash
# Windows (MinGW)
gcc -o snmp_agent.exe snmp_agent.c -lws2_32
gcc -o snmp_client.exe snmp_client.c -lws2_32

# Windows (MSVC)
cl /Fe:snmp_agent.exe snmp_agent.c ws2_32.lib
cl /Fe:snmp_client.exe snmp_client.c ws2_32.lib

# O usa el script
compile.bat
```

## Uso

### 1. Ejecutar el Agente
```bash
# Como Administrador (requiere puerto 161)
snmp_agent.exe
```

### 2. Probar con el Cliente
```bash
snmp_client.exe
# Selecciona un OID del menú para consultar
```

### 3. Consultas con snmpget (opcional)
```bash
# Si tienes net-snmp instalado
snmpget -v2c -c public localhost:161 1.3.6.1.2.1.1.1.0
```

## OIDs Disponibles

**Sistema (MIB-II):**
- `1.3.6.1.2.1.1.1.0` - sysDescr (Descripción del sistema)
- `1.3.6.1.2.1.1.3.0` - sysUpTime (Tiempo de actividad)
- `1.3.6.1.2.1.1.5.0` - sysName (Nombre del sistema)
- `1.3.6.1.2.1.1.4.0` - sysContact (Contacto)
- `1.3.6.1.2.1.1.6.0` - sysLocation (Ubicación)

**Métricas de Servicios:**
- `1.3.6.1.4.1.99999.1.1.0` - Peticiones FTP
- `1.3.6.1.4.1.99999.1.2.0` - Peticiones HTTP
- `1.3.6.1.4.1.99999.1.3.0` - Mensajes SMTP
- `1.3.6.1.4.1.99999.2.1.0` - Uso de CPU (%)
- `1.3.6.1.4.1.99999.2.2.0` - Uso de Memoria (%)
- `1.3.6.1.4.1.99999.3.1.0` - Contador de errores

## Notas

- El agente requiere ejecutarse como **Administrador** para usar el puerto 161
- Community por defecto: `public`
- Los traps se envían automáticamente a `127.0.0.1:162` cuando se superan umbrales
- Implementación educativa con codificación BER/ASN.1 simplificada
