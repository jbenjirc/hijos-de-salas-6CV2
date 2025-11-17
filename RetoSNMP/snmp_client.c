/* snmp_client.c - Cliente SNMP simple para pruebas (Windows)
   
   Cliente básico para probar el agente SNMP
   Envía GetRequest y muestra la respuesta
   
   Compilar (MinGW): gcc -Wall -o snmp_client.exe snmp_client.c -lws2_32
   Compilar (MSVC):  cl /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:snmp_client.exe snmp_client.c ws2_32.lib
   
   Uso: snmp_client.exe [host] [oid]
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
#define BUFFER_SIZE 2048

// OIDs de sistema
#define OID_SYSDESCR      "1.3.6.1.2.1.1.1.0"
#define OID_SYSOBJECTID   "1.3.6.1.2.1.1.2.0"
#define OID_SYSUPTIME     "1.3.6.1.2.1.1.3.0"
#define OID_SYSCONTACT    "1.3.6.1.2.1.1.4.0"
#define OID_SYSNAME       "1.3.6.1.2.1.1.5.0"
#define OID_SYSLOCATION   "1.3.6.1.2.1.1.6.0"

// OIDs personalizados
#define OID_FTP_REQUESTS   "1.3.6.1.4.1.99999.1.1.0"
#define OID_HTTP_REQUESTS  "1.3.6.1.4.1.99999.1.2.0"
#define OID_SMTP_MESSAGES  "1.3.6.1.4.1.99999.1.3.0"
#define OID_CPU_USAGE      "1.3.6.1.4.1.99999.2.1.0"
#define OID_MEM_USAGE      "1.3.6.1.4.1.99999.2.2.0"

// Tipos ASN.1
#define ASN_INTEGER     0x02
#define ASN_OCTETSTR    0x04
#define ASN_NULL        0x05
#define ASN_OBJECTID    0x06
#define ASN_SEQUENCE    0x30
#define PDU_GET_REQUEST 0xA0
#define PDU_GET_RESPONSE 0xA2

void print_menu(void) {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║        CLIENTE SNMP - PROYECTO REDES ESCOM           ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");
    
    printf("OIDs disponibles para consulta:\n\n");
    printf("Sistema (MIB-II):\n");
    printf("  1) %s  - Descripción del sistema\n", OID_SYSDESCR);
    printf("  2) %s  - Tiempo de actividad\n", OID_SYSUPTIME);
    printf("  3) %s  - Nombre del sistema\n", OID_SYSNAME);
    printf("  4) %s  - Contacto del sistema\n", OID_SYSCONTACT);
    printf("  5) %s  - Ubicación del sistema\n\n", OID_SYSLOCATION);
    
    printf("Métricas de Servicios:\n");
    printf("  6) %s - Peticiones FTP\n", OID_FTP_REQUESTS);
    printf("  7) %s - Peticiones HTTP\n", OID_HTTP_REQUESTS);
    printf("  8) %s - Mensajes SMTP\n\n", OID_SMTP_MESSAGES);
    
    printf("Métricas del Sistema:\n");
    printf("  9) %s - Uso de CPU (%%)\n", OID_CPU_USAGE);
    printf(" 10) %s - Uso de Memoria (%%)\n\n", OID_MEM_USAGE);
    
    printf(" 11) Consultar todos los OIDs\n");
    printf("  0) Salir\n\n");
}

const char* get_oid_by_number(int num) {
    switch (num) {
        case 1: return OID_SYSDESCR;
        case 2: return OID_SYSUPTIME;
        case 3: return OID_SYSNAME;
        case 4: return OID_SYSCONTACT;
        case 5: return OID_SYSLOCATION;
        case 6: return OID_FTP_REQUESTS;
        case 7: return OID_HTTP_REQUESTS;
        case 8: return OID_SMTP_MESSAGES;
        case 9: return OID_CPU_USAGE;
        case 10: return OID_MEM_USAGE;
        default: return NULL;
    }
}

const char* get_oid_name(const char* oid) {
    if (strcmp(oid, OID_SYSDESCR) == 0) return "sysDescr";
    if (strcmp(oid, OID_SYSUPTIME) == 0) return "sysUpTime";
    if (strcmp(oid, OID_SYSNAME) == 0) return "sysName";
    if (strcmp(oid, OID_SYSCONTACT) == 0) return "sysContact";
    if (strcmp(oid, OID_SYSLOCATION) == 0) return "sysLocation";
    if (strcmp(oid, OID_FTP_REQUESTS) == 0) return "ftpRequests";
    if (strcmp(oid, OID_HTTP_REQUESTS) == 0) return "httpRequests";
    if (strcmp(oid, OID_SMTP_MESSAGES) == 0) return "smtpMessages";
    if (strcmp(oid, OID_CPU_USAGE) == 0) return "cpuUsage";
    if (strcmp(oid, OID_MEM_USAGE) == 0) return "memUsage";
    return "unknown";
}

int encode_length(unsigned char *buf, int len) {
    if (len < 128) {
        buf[0] = (unsigned char)len;
        return 1;
    } else {
        buf[0] = 0x81;
        buf[1] = (unsigned char)len;
        return 2;
    }
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
    return encode_string(buf, oid_str);
}

int encode_null(unsigned char *buf) {
    buf[0] = ASN_NULL;
    buf[1] = 0;
    return 2;
}

int build_get_request(unsigned char *buf, const char *oid, int request_id) {
    int offset = 0;
    
    // Mensaje SNMP
    buf[offset++] = ASN_SEQUENCE;
    int msg_len_pos = offset++;
    
    // Version (v2c = 1)
    offset += encode_integer(buf + offset, 1);
    
    // Community
    offset += encode_string(buf + offset, "public");
    
    // PDU GetRequest
    buf[offset++] = PDU_GET_REQUEST;
    int pdu_len_pos = offset++;
    
    // Request ID
    offset += encode_integer(buf + offset, request_id);
    
    // Error status
    offset += encode_integer(buf + offset, 0);
    
    // Error index
    offset += encode_integer(buf + offset, 0);
    
    // Varbind list
    buf[offset++] = ASN_SEQUENCE;
    int varbind_list_pos = offset++;
    
    // Varbind
    buf[offset++] = ASN_SEQUENCE;
    int varbind_pos = offset++;
    
    // OID
    offset += encode_oid(buf + offset, oid);
    
    // Value (NULL para GetRequest)
    offset += encode_null(buf + offset);
    
    // Actualizar longitudes
    buf[varbind_pos] = (unsigned char)(offset - varbind_pos - 1);
    buf[varbind_list_pos] = (unsigned char)(offset - varbind_list_pos - 1);
    buf[pdu_len_pos] = (unsigned char)(offset - pdu_len_pos - 1);
    buf[msg_len_pos] = (unsigned char)(offset - msg_len_pos - 1);
    
    return offset;
}

void parse_response(unsigned char *buf, int len) {
    // Parser simplificado (solo muestra datos en hexadecimal y texto)
    printf("\nRespuesta recibida (%d bytes):\n", len);
    printf("Hex: ");
    for (int i = 0; i < (len > 64 ? 64 : len); i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n     ");
    }
    if (len > 64) printf("...");
    printf("\n");
    
    // Intentar extraer texto (simplificado)
    printf("\nTexto extraído: ");
    int in_string = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == ASN_OCTETSTR && i + 2 < len) {
            int str_len = buf[i + 1];
            if (str_len > 0 && str_len < 200 && i + 2 + str_len <= len) {
                printf("\"");
                for (int j = 0; j < str_len; j++) {
                    char c = (char)buf[i + 2 + j];
                    if (c >= 32 && c < 127) {
                        printf("%c", c);
                    }
                }
                printf("\" ");
                i += 1 + str_len;
                in_string = 1;
            }
        }
    }
    if (!in_string) {
        printf("(datos binarios)");
    }
    printf("\n");
}

int send_snmp_get(const char *host, const char *oid) {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    unsigned char request[BUFFER_SIZE];
    unsigned char response[BUFFER_SIZE];
    int request_id = (int)time(NULL);
    
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Error inicializando Winsock\n");
        return 0;
    }
    
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Error creando socket\n");
        WSACleanup();
        return 0;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(host);
    server_addr.sin_port = htons(SNMP_PORT);
    
    // Construir request
    int req_len = build_get_request(request, oid, request_id);
    
    printf("\n════════════════════════════════════════════════════════\n");
    printf("Enviando GetRequest:\n");
    printf("  Host:      %s:%d\n", host, SNMP_PORT);
    printf("  OID:       %s (%s)\n", oid, get_oid_name(oid));
    printf("  Community: public\n");
    printf("════════════════════════════════════════════════════════\n");
    
    // Enviar
    if (sendto(sock, (char*)request, req_len, 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Error enviando request: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 0;
    }
    
    // Recibir respuesta con timeout
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    
    int addr_len = sizeof(server_addr);
    int bytes = recvfrom(sock, (char*)response, sizeof(response), 0,
                        (struct sockaddr*)&server_addr, &addr_len);
    
    if (bytes > 0) {
        parse_response(response, bytes);
        printf("════════════════════════════════════════════════════════\n");
    } else {
        printf("\n[ERROR] No se recibió respuesta (timeout o error)\n");
        printf("        Verifique que el agente SNMP esté corriendo.\n");
        printf("════════════════════════════════════════════════════════\n");
    }
    
    closesocket(sock);
    WSACleanup();
    return (bytes > 0);
}

int main(int argc, char *argv[]) {
    char host[64] = "127.0.0.1";
    
    SetConsoleOutputCP(65001);
    
    if (argc > 1) {
        strncpy(host, argv[1], sizeof(host) - 1);
    }
    
    int choice;
    while (1) {
        print_menu();
        printf("Seleccione OID (0 para salir): ");
        
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("\nEntrada inválida.\n");
            continue;
        }
        
        if (choice == 0) {
            printf("\nSaliendo...\n");
            break;
        }
        
        if (choice == 11) {
            // Consultar todos
            printf("\n\n╔════════════════════════════════════════════════════════╗\n");
            printf("║         CONSULTANDO TODOS LOS OIDs                    ║\n");
            printf("╚════════════════════════════════════════════════════════╝\n");
            
            for (int i = 1; i <= 10; i++) {
                const char* oid = get_oid_by_number(i);
                if (oid) {
                    send_snmp_get(host, oid);
                    Sleep(500); // Pausa entre requests
                }
            }
            
            printf("\nPresione ENTER para continuar...");
            while (getchar() != '\n');
            getchar();
        } else {
            const char* oid = get_oid_by_number(choice);
            if (oid) {
                send_snmp_get(host, oid);
                printf("\nPresione ENTER para continuar...");
                while (getchar() != '\n');
                getchar();
            } else {
                printf("\nOpción inválida.\n");
            }
        }
    }
    
    return 0;
}
