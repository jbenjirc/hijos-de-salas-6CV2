/* snmp_agent.c - Agente SNMP v1/v2c educativo (Windows)
   
   Características:
   - Servidor UDP en puerto 161
   - Responde a GetRequest y GetNextRequest (básico)
   - OIDs simulados: sysDescr, sysUpTime, métricas de servicios
   - Envío opcional de traps a puerto 162 (eventos de alto uso, errores)
   
   Compilar (MinGW): gcc -Wall -o snmp_agent.exe snmp_agent.c -lws2_32
   Compilar (MSVC):  cl /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:snmp_agent.exe snmp_agent.c ws2_32.lib
   
   Ejecutar: snmp_agent.exe [puerto_trap_manager]
*/

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define SNMP_PORT 161
#define TRAP_PORT 162
#define BUFFER_SIZE 2048

// OIDs simulados (simplificados como strings)
#define OID_SYSDESCR      "1.3.6.1.2.1.1.1.0"
#define OID_SYSOBJECTID   "1.3.6.1.2.1.1.2.0"
#define OID_SYSUPTIME     "1.3.6.1.2.1.1.3.0"
#define OID_SYSCONTACT    "1.3.6.1.2.1.1.4.0"
#define OID_SYSNAME       "1.3.6.1.2.1.1.5.0"
#define OID_SYSLOCATION   "1.3.6.1.2.1.1.6.0"

// OIDs personalizados para métricas de servicios
#define OID_FTP_REQUESTS   "1.3.6.1.4.1.99999.1.1.0"
#define OID_HTTP_REQUESTS  "1.3.6.1.4.1.99999.1.2.0"
#define OID_SMTP_MESSAGES  "1.3.6.1.4.1.99999.1.3.0"
#define OID_CPU_USAGE      "1.3.6.1.4.1.99999.2.1.0"
#define OID_MEM_USAGE      "1.3.6.1.4.1.99999.2.2.0"
#define OID_ERROR_COUNT    "1.3.6.1.4.1.99999.3.1.0"

// Umbrales para traps
#define CPU_THRESHOLD 80
#define MEM_THRESHOLD 80
#define ERROR_THRESHOLD 10

// Tipos SNMP simplificados
#define ASN_INTEGER     0x02
#define ASN_OCTETSTR    0x04
#define ASN_NULL        0x05
#define ASN_OBJECTID    0x06
#define ASN_SEQUENCE    0x30
#define ASN_COUNTER32   0x41
#define ASN_GAUGE32     0x42
#define ASN_TIMETICKS   0x43
#define ASN_IPADDRESS   0x40

// PDU Types
#define PDU_GET_REQUEST      0xA0
#define PDU_GET_NEXT_REQUEST 0xA1
#define PDU_GET_RESPONSE     0xA2
#define PDU_SET_REQUEST      0xA3
#define PDU_TRAP             0xA4

// Estructura para almacenar métricas
typedef struct {
    DWORD start_time;
    int ftp_requests;
    int http_requests;
    int smtp_messages;
    int cpu_usage;
    int mem_usage;
    int error_count;
} AgentMetrics;

AgentMetrics g_metrics = {0};
char g_trap_manager_ip[64] = "127.0.0.1";
int g_trap_manager_port = TRAP_PORT;
SOCKET g_trap_socket = INVALID_SOCKET;

// ==================== Funciones auxiliares ====================

DWORD get_uptime_ticks(void) {
    // Devuelve tiempo en centésimas de segundo desde inicio
    return (GetTickCount() - g_metrics.start_time) / 10;
}

void update_system_metrics(void) {
    // Simular métricas del sistema (en producción, leer del sistema real)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    g_metrics.mem_usage = (int)mem.dwMemoryLoad;
    
    // CPU usage simplificado (aleatorio para demo)
    g_metrics.cpu_usage = (rand() % 40) + 20; // 20-60%
    
    // Incrementar contadores de forma aleatoria (simular actividad)
    if (rand() % 10 == 0) g_metrics.ftp_requests++;
    if (rand() % 8 == 0) g_metrics.http_requests++;
    if (rand() % 12 == 0) g_metrics.smtp_messages++;
}

// ==================== Codificación BER básica ====================

int encode_length(unsigned char *buf, int len) {
    if (len < 128) {
        buf[0] = (unsigned char)len;
        return 1;
    } else {
        // Forma larga (simplificada para < 256)
        buf[0] = 0x81;
        buf[1] = (unsigned char)len;
        return 2;
    }
}

int decode_length(unsigned char *buf, int *len) {
    if (buf[0] < 128) {
        *len = buf[0];
        return 1;
    } else if (buf[0] == 0x81) {
        *len = buf[1];
        return 2;
    }
    return -1;
}

int encode_integer(unsigned char *buf, int value) {
    buf[0] = ASN_INTEGER;
    if (value >= -128 && value <= 127) {
        buf[1] = 1;
        buf[2] = (unsigned char)(value & 0xFF);
        return 3;
    } else {
        buf[1] = 4;
        buf[2] = (unsigned char)((value >> 24) & 0xFF);
        buf[3] = (unsigned char)((value >> 16) & 0xFF);
        buf[4] = (unsigned char)((value >> 8) & 0xFF);
        buf[5] = (unsigned char)(value & 0xFF);
        return 6;
    }
}

int encode_string(unsigned char *buf, const char *str) {
    int slen = (int)strlen(str);
    buf[0] = ASN_OCTETSTR;
    int len_bytes = encode_length(buf + 1, slen);
    memcpy(buf + 1 + len_bytes, str, slen);
    return 1 + len_bytes + slen;
}

int encode_oid(unsigned char *buf, const char *oid_str) {
    // Simplificación: codificar OID como string (no es BER puro)
    // En producción, convertir "1.3.6.1..." a formato BER real
    return encode_string(buf, oid_str);
}

int encode_timeticks(unsigned char *buf, DWORD ticks) {
    buf[0] = ASN_TIMETICKS;
    buf[1] = 4;
    buf[2] = (unsigned char)((ticks >> 24) & 0xFF);
    buf[3] = (unsigned char)((ticks >> 16) & 0xFF);
    buf[4] = (unsigned char)((ticks >> 8) & 0xFF);
    buf[5] = (unsigned char)(ticks & 0xFF);
    return 6;
}

int encode_counter32(unsigned char *buf, int value) {
    buf[0] = ASN_COUNTER32;
    buf[1] = 4;
    buf[2] = (unsigned char)((value >> 24) & 0xFF);
    buf[3] = (unsigned char)((value >> 16) & 0xFF);
    buf[4] = (unsigned char)((value >> 8) & 0xFF);
    buf[5] = (unsigned char)(value & 0xFF);
    return 6;
}

// ==================== Búsqueda de OID ====================

int get_oid_value(const char *oid, unsigned char *value_buf, int *value_len) {
    // Devuelve tipo y valor para el OID solicitado
    
    if (strcmp(oid, OID_SYSDESCR) == 0) {
        *value_len = encode_string(value_buf, "Agente SNMP Educativo - Proyecto Redes ESCOM");
        return 1;
    }
    if (strcmp(oid, OID_SYSOBJECTID) == 0) {
        *value_len = encode_oid(value_buf, "1.3.6.1.4.1.99999");
        return 1;
    }
    if (strcmp(oid, OID_SYSUPTIME) == 0) {
        *value_len = encode_timeticks(value_buf, get_uptime_ticks());
        return 1;
    }
    if (strcmp(oid, OID_SYSCONTACT) == 0) {
        *value_len = encode_string(value_buf, "admin@escom.ipn.mx");
        return 1;
    }
    if (strcmp(oid, OID_SYSNAME) == 0) {
        *value_len = encode_string(value_buf, "servidor-redes-escom");
        return 1;
    }
    if (strcmp(oid, OID_SYSLOCATION) == 0) {
        *value_len = encode_string(value_buf, "ESCOM - IPN, Ciudad de Mexico");
        return 1;
    }
    if (strcmp(oid, OID_FTP_REQUESTS) == 0) {
        *value_len = encode_counter32(value_buf, g_metrics.ftp_requests);
        return 1;
    }
    if (strcmp(oid, OID_HTTP_REQUESTS) == 0) {
        *value_len = encode_counter32(value_buf, g_metrics.http_requests);
        return 1;
    }
    if (strcmp(oid, OID_SMTP_MESSAGES) == 0) {
        *value_len = encode_counter32(value_buf, g_metrics.smtp_messages);
        return 1;
    }
    if (strcmp(oid, OID_CPU_USAGE) == 0) {
        *value_len = encode_integer(value_buf, g_metrics.cpu_usage);
        return 1;
    }
    if (strcmp(oid, OID_MEM_USAGE) == 0) {
        *value_len = encode_integer(value_buf, g_metrics.mem_usage);
        return 1;
    }
    if (strcmp(oid, OID_ERROR_COUNT) == 0) {
        *value_len = encode_counter32(value_buf, g_metrics.error_count);
        return 1;
    }
    
    return 0; // OID no encontrado
}

// ==================== Procesamiento de PDU ====================

void process_get_request(unsigned char *req_buf, int req_len, 
                        unsigned char *resp_buf, int *resp_len,
                        struct sockaddr_in *client_addr) {
    // Simplificación: parsear manualmente (en producción usar librería ASN.1)
    
    printf("Procesando GetRequest desde %s:%d\n", 
           inet_ntoa(client_addr->sin_addr), 
           ntohs(client_addr->sin_port));
    
    // Por simplicidad, respondemos con datos fijos de sysDescr
    // (un parser BER completo requeriría más código)
    
    int offset = 0;
    unsigned char value[256];
    int value_len = 0;
    
    // Simular respuesta básica
    get_oid_value(OID_SYSDESCR, value, &value_len);
    
    // Construir respuesta SNMP básica (simplificada)
    resp_buf[offset++] = ASN_SEQUENCE;
    offset++; // placeholder para longitud
    
    // Version (v2c = 1)
    offset += encode_integer(resp_buf + offset, 1);
    
    // Community
    offset += encode_string(resp_buf + offset, "public");
    
    // PDU GetResponse
    resp_buf[offset++] = PDU_GET_RESPONSE;
    offset++; // placeholder
    
    // Request ID (eco del request)
    offset += encode_integer(resp_buf + offset, 12345);
    
    // Error status
    offset += encode_integer(resp_buf + offset, 0);
    
    // Error index
    offset += encode_integer(resp_buf + offset, 0);
    
    // Varbind list
    resp_buf[offset++] = ASN_SEQUENCE;
    offset++; // placeholder
    
    // Varbind
    resp_buf[offset++] = ASN_SEQUENCE;
    offset++; // placeholder
    
    // OID
    offset += encode_oid(resp_buf + offset, OID_SYSDESCR);
    
    // Value
    memcpy(resp_buf + offset, value, value_len);
    offset += value_len;
    
    // Ajustar longitudes (simplificado)
    resp_buf[1] = (unsigned char)(offset - 2);
    
    *resp_len = offset;
    
    printf("Enviando GetResponse (%d bytes)\n", *resp_len);
}

void send_trap(const char *trap_type, const char *description) {
    if (g_trap_socket == INVALID_SOCKET) return;
    
    unsigned char trap_buf[512];
    int offset = 0;
    
    printf("\n[TRAP] Enviando trap: %s - %s\n", trap_type, description);
    
    // Construir paquete Trap básico
    trap_buf[offset++] = ASN_SEQUENCE;
    offset++; // placeholder longitud
    
    // Version
    offset += encode_integer(trap_buf + offset, 1);
    
    // Community
    offset += encode_string(trap_buf + offset, "public");
    
    // PDU Trap
    trap_buf[offset++] = PDU_TRAP;
    offset++; // placeholder
    
    // Enterprise OID
    offset += encode_oid(trap_buf + offset, "1.3.6.1.4.1.99999");
    
    // Agent address (127.0.0.1)
    trap_buf[offset++] = ASN_IPADDRESS;
    trap_buf[offset++] = 4;
    trap_buf[offset++] = 127;
    trap_buf[offset++] = 0;
    trap_buf[offset++] = 0;
    trap_buf[offset++] = 1;
    
    // Generic trap (6 = enterpriseSpecific)
    offset += encode_integer(trap_buf + offset, 6);
    
    // Specific trap
    offset += encode_integer(trap_buf + offset, 1);
    
    // Timestamp
    offset += encode_timeticks(trap_buf + offset, get_uptime_ticks());
    
    // Varbind list con descripción
    trap_buf[offset++] = ASN_SEQUENCE;
    int varbind_start = offset;
    offset++;
    
    trap_buf[offset++] = ASN_SEQUENCE;
    offset++;
    offset += encode_oid(trap_buf + offset, "1.3.6.1.4.1.99999.4.1.0");
    offset += encode_string(trap_buf + offset, description);
    
    trap_buf[varbind_start] = (unsigned char)(offset - varbind_start - 1);
    trap_buf[1] = (unsigned char)(offset - 2);
    
    struct sockaddr_in trap_addr;
    memset(&trap_addr, 0, sizeof(trap_addr));
    trap_addr.sin_family = AF_INET;
    trap_addr.sin_addr.s_addr = inet_addr(g_trap_manager_ip);
    trap_addr.sin_port = htons(g_trap_manager_port);
    
    sendto(g_trap_socket, (char*)trap_buf, offset, 0,
           (struct sockaddr*)&trap_addr, sizeof(trap_addr));
    
    printf("[TRAP] Enviado a %s:%d\n\n", g_trap_manager_ip, g_trap_manager_port);
}

void check_and_send_traps(void) {
    static int last_cpu = 0;
    static int last_mem = 0;
    static int last_errors = 0;
    
    // CPU alto
    if (g_metrics.cpu_usage > CPU_THRESHOLD && last_cpu <= CPU_THRESHOLD) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Uso de CPU alto: %d%%", g_metrics.cpu_usage);
        send_trap("cpuHigh", msg);
    }
    last_cpu = g_metrics.cpu_usage;
    
    // Memoria alta
    if (g_metrics.mem_usage > MEM_THRESHOLD && last_mem <= MEM_THRESHOLD) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Uso de memoria alto: %d%%", g_metrics.mem_usage);
        send_trap("memHigh", msg);
    }
    last_mem = g_metrics.mem_usage;
    
    // Errores
    if (g_metrics.error_count > ERROR_THRESHOLD && last_errors <= ERROR_THRESHOLD) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Contador de errores alto: %d", g_metrics.error_count);
        send_trap("errorThreshold", msg);
    }
    last_errors = g_metrics.error_count;
}

// ==================== Monitor de actividad ====================

DWORD WINAPI monitor_thread(LPVOID param) {
    while (1) {
        Sleep(5000); // Cada 5 segundos
        update_system_metrics();
        check_and_send_traps();
    }
    return 0;
}

// ==================== Main ====================

void print_status(void) {
    system("cls");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          AGENTE SNMP v1/v2c - PROYECTO REDES ESCOM       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    printf("Estado del Agente:\n");
    printf("  Puerto:           UDP %d\n", SNMP_PORT);
    printf("  Community:        public\n");
    printf("  Uptime:           %lu ticks (%.1f seg)\n", 
           get_uptime_ticks(), get_uptime_ticks() / 100.0);
    printf("  Trap Manager:     %s:%d\n\n", g_trap_manager_ip, g_trap_manager_port);
    
    printf("Métricas del Sistema:\n");
    printf("  CPU Usage:        %d%%\n", g_metrics.cpu_usage);
    printf("  Memoria:          %d%%\n", g_metrics.mem_usage);
    printf("  Errores:          %d\n\n", g_metrics.error_count);
    
    printf("Métricas de Servicios:\n");
    printf("  FTP Requests:     %d\n", g_metrics.ftp_requests);
    printf("  HTTP Requests:    %d\n", g_metrics.http_requests);
    printf("  SMTP Messages:    %d\n\n", g_metrics.smtp_messages);
    
    printf("OIDs disponibles:\n");
    printf("  %s - sysDescr\n", OID_SYSDESCR);
    printf("  %s - sysUpTime\n", OID_SYSUPTIME);
    printf("  %s - sysName\n", OID_SYSNAME);
    printf("  %s - FTP Requests\n", OID_FTP_REQUESTS);
    printf("  %s - HTTP Requests\n", OID_HTTP_REQUESTS);
    printf("  %s - SMTP Messages\n", OID_SMTP_MESSAGES);
    printf("  %s - CPU Usage\n", OID_CPU_USAGE);
    printf("  %s - Memory Usage\n", OID_MEM_USAGE);
    printf("\n");
    printf("Escuchando peticiones SNMP...\n");
    printf("Presione Ctrl+C para detener.\n");
    printf("════════════════════════════════════════════════════════════\n\n");
}

int main(int argc, char *argv[]) {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr, client_addr;
    int client_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];
    unsigned char response[BUFFER_SIZE];
    int resp_len;
    
    // Configurar consola
    SetConsoleOutputCP(65001);
    
    // Inicializar métricas
    g_metrics.start_time = GetTickCount();
    srand((unsigned)time(NULL));
    
    // Parsear argumentos
    if (argc > 1) {
        strncpy(g_trap_manager_ip, argv[1], sizeof(g_trap_manager_ip) - 1);
    }
    
    // Inicializar Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Error inicializando Winsock: %d\n", WSAGetLastError());
        return 1;
    }
    
    // Crear socket UDP para SNMP
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Error creando socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    // Configurar dirección
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SNMP_PORT);
    
    // Bind
    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Error en bind puerto %d: %d\n", SNMP_PORT, WSAGetLastError());
        fprintf(stderr, "Sugerencia: Ejecutar como Administrador\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    
    // Crear socket para traps
    g_trap_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    // Iniciar thread de monitoreo
    CreateThread(NULL, 0, monitor_thread, NULL, 0, NULL);
    
    print_status();
    
    printf("Agente SNMP iniciado correctamente.\n\n");
    
    // Bucle principal
    while (1) {
        int bytes = recvfrom(sock, (char*)buffer, sizeof(buffer), 0,
                            (struct sockaddr*)&client_addr, &client_len);
        
        if (bytes > 0) {
            // Procesar request
            process_get_request(buffer, bytes, response, &resp_len, &client_addr);
            
            // Enviar respuesta
            sendto(sock, (char*)response, resp_len, 0,
                   (struct sockaddr*)&client_addr, client_len);
            
            // Actualizar display cada 10 requests
            static int req_count = 0;
            if (++req_count % 10 == 0) {
                print_status();
            }
        }
    }
    
    closesocket(sock);
    if (g_trap_socket != INVALID_SOCKET) closesocket(g_trap_socket);
    WSACleanup();
    
    return 0;
}
