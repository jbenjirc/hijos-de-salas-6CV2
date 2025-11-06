// cliente.c - Cliente combinado HTTP + FTP (Windows) con autenticación
// Compilar:
//   gcc cliente.c -o cliente.exe -lws2_32
//
// Requisitos:
// - ftp_server.exe corriendo en localhost:21
// - server.exe (HTTP) corriendo en localhost:8080
//
// Este cliente permite:
//  - HTTP: GET, HEAD, POST (solo a /api/echo con plaintext)
//  - FTP: Archivos compartidos (LIST, RETR, STOR)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define HTTP_SERVER_IP "127.0.0.1"
#define HTTP_SERVER_PORT 8080
#define BUFFER_SIZE 8192

// FTP defaults
#define FTP_SERVER_IP "127.0.0.1"
#define FTP_SERVER_PORT 21
#define FTP_BUFFER_SIZE 8192

// ---------------------- Funciones de autenticación ----------------------
// Codificación simple de Base64
void base64_encode(const char *input, char *output, int len) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];
    
    while (len--) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                output[j++] = base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for (int k = i; k < 3; k++)
            char_array_3[k] = '\0';
            
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (int k = 0; k < i + 1; k++)
            output[j++] = base64_chars[char_array_4[k]];
            
        while (i++ < 3)
            output[j++] = '=';
    }
    
    output[j] = '\0';
}

// Verificar si una ruta requiere autenticación
int requires_auth(const char *path) {
    return (strcmp(path, "/status") == 0 || 
            strcmp(path, "/upload") == 0 ||
            strncmp(path, "/api/", 5) == 0);
}

// ---------------------- HTTP client con autenticación ----------------------
void send_http_request(const char *method, const char *path, const char *body, const char *username, const char *password) {
    WSADATA wsa;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in server;
    char request[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    int bytes;

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Winsock init failed\n");
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Socket error\n"); WSACleanup(); return;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(HTTP_SERVER_IP);
    server.sin_port = htons(HTTP_SERVER_PORT);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Could not connect to HTTP server %s:%d (error %d)\n", HTTP_SERVER_IP, HTTP_SERVER_PORT, WSAGetLastError());
        closesocket(sock); WSACleanup(); return;
    }

    // Preparar credenciales si se necesitan
    char auth_header[256] = "";
    if (requires_auth(path) && username && password && strlen(username) > 0) {
        char credentials[128];
        char encoded[256];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
        base64_encode(credentials, encoded, (int)strlen(credentials));
        snprintf(auth_header, sizeof(auth_header), "Authorization: Basic %s\r\n", encoded);
    }

    if (_stricmp(method, "POST") == 0) {
        int len = body ? (int)strlen(body) : 0;
        snprintf(request, sizeof(request),
            "POST %s HTTP/1.1\r\nHost: %s\r\n%sContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            path, HTTP_SERVER_IP, auth_header, len, body ? body : "");
    } else {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\nHost: %s\r\n%sConnection: close\r\n\r\n",
            method, path, HTTP_SERVER_IP, auth_header);
    }

    send(sock, request, (int)strlen(request), 0);
    printf("\n--- Request sent ---\n%s\n", request);
    printf("\n--- Response ---\n");

    while ((bytes = recv(sock, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }
    printf("\n--- End ---\n");

    closesocket(sock);
    WSACleanup();
}

// ---------------------- Minimal FTP client (enfocado a STOR/RETR/LIST) ----------------------
typedef struct {
    SOCKET ctrl_sock;
    SOCKET data_sock;
    char pasv_ip[64];
    int pasv_port;
} FTPC;

// enviar comando al socket de control
int ftp_send_cmd(FTPC *c, const char *cmd) {
    char buf[FTP_BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    return send(c->ctrl_sock, buf, (int)strlen(buf), 0);
}

// recibir respuesta (simple, lee una vez). retorna código numérico
int ftp_recv_response(FTPC *c, char *out, int outlen) {
    int bytes = recv(c->ctrl_sock, out, outlen-1, 0);
    if (bytes <= 0) { out[0]='\0'; return 0; }
    out[bytes] = '\0';
    // imprimir
    printf("<< %s", out);
    // parsear código
    char code_s[4] = {0};
    if (bytes >= 3) {
        memcpy(code_s, out, 3);
        return atoi(code_s);
    }
    return 0;
}

int ftp_connect(FTPC *c, const char *server_ip, int port) {
    WSADATA wsa;
    struct sockaddr_in addr;
    char resp[FTP_BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Winsock init failed\n"); return 0;
    }

    c->ctrl_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (c->ctrl_sock == INVALID_SOCKET) {
        printf("Error creating socket: %d\n", WSAGetLastError()); WSACleanup(); return 0;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(server_ip);
    addr.sin_port = htons(port);

    printf("Conectando FTP %s:%d ...\n", server_ip, port);
    if (connect(c->ctrl_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Connect error: %d\n", WSAGetLastError());
        closesocket(c->ctrl_sock); WSACleanup(); return 0;
    }

    // recibir banner 220
    int code = ftp_recv_response(c, resp, sizeof(resp));
    return (code == 220 || code == 120 || code == 421 || code != 0);
}

int ftp_login(FTPC *c, const char *user, const char *pass) {
    char resp[FTP_BUFFER_SIZE];
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "USER %s", user);
    ftp_send_cmd(c, cmd);
    int code = ftp_recv_response(c, resp, sizeof(resp));
    if (code != 331 && code != 230) {
        printf("USER not accepted\n"); return 0;
    }
    if (code == 230) return 1; // ya logged

    snprintf(cmd, sizeof(cmd), "PASS %s", pass);
    ftp_send_cmd(c, cmd);
    code = ftp_recv_response(c, resp, sizeof(resp));
    return (code == 230);
}

int ftp_enter_passive(FTPC *c) {
    char resp[FTP_BUFFER_SIZE];
    ftp_send_cmd(c, "PASV");
    int code = ftp_recv_response(c, resp, sizeof(resp));
    if (code != 227) { printf("PASV failed code=%d\n", code); return 0; }

    // parse "(h1,h2,h3,h4,p1,p2)"
    char *p = strchr(resp, '(');
    if (!p) return 0;
    int h1,h2,h3,h4,p1,p2;
    if (sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1,&h2,&h3,&h4,&p1,&p2) != 6) return 0;
    snprintf(c->pasv_ip, sizeof(c->pasv_ip), "%d.%d.%d.%d", h1,h2,h3,h4);
    c->pasv_port = p1*256 + p2;
    return 1;
}

int ftp_connect_data_socket(FTPC *c) {
    struct sockaddr_in addr;
    c->data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (c->data_sock == INVALID_SOCKET) {
        printf("Data socket error: %d\n", WSAGetLastError()); return 0;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(c->pasv_ip);
    addr.sin_port = htons(c->pasv_port);
    if (connect(c->data_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Data connect error: %d\n", WSAGetLastError());
        closesocket(c->data_sock); c->data_sock = INVALID_SOCKET; return 0;
    }
    return 1;
}

int ftp_upload_file(FTPC *c, const char *local_path, const char *remote_name) {
    char resp[FTP_BUFFER_SIZE];
    char cmd[512];

    // abrir local
    FILE *f = fopen(local_path, "rb");
    if (!f) { printf("Local file not found: %s\n", local_path); return 0; }

    if (!ftp_enter_passive(c)) { fclose(f); return 0; }
    if (!ftp_connect_data_socket(c)) { fclose(f); return 0; }

    snprintf(cmd, sizeof(cmd), "STOR %s", remote_name);
    ftp_send_cmd(c, cmd);
    int code = ftp_recv_response(c, resp, sizeof(resp));
    if (code != 150 && code != 125) { printf("STOR refused (code=%d)\n", code); closesocket(c->data_sock); fclose(f); return 0; }

    // enviar datos
    char buf[FTP_BUFFER_SIZE];
    size_t n;
    long total = 0;
    clock_t start = clock();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        int sent = send(c->data_sock, buf, (int)n, 0);
        if (sent <= 0) { printf("Error sending data\n"); break; }
        total += sent;
    }
    double dur = (double)(clock()-start)/CLOCKS_PER_SEC;

    closesocket(c->data_sock);
    c->data_sock = INVALID_SOCKET;
    fclose(f);

    // leer respuesta final (226)
    ftp_recv_response(c, resp, sizeof(resp));
    printf("Upload finished: %ld bytes in %.2f s\n", total, dur);
    return (total > 0);
}

int ftp_download_file(FTPC *c, const char *remote, const char *local) {
    char resp[FTP_BUFFER_SIZE];
    char cmd[512];

    if (!ftp_enter_passive(c)) return 0;
    if (!ftp_connect_data_socket(c)) return 0;

    snprintf(cmd, sizeof(cmd), "RETR %s", remote);
    ftp_send_cmd(c, cmd);
    int code = ftp_recv_response(c, resp, sizeof(resp));
    if (code != 150 && code != 125) { printf("RETR refused (code=%d)\n", code); closesocket(c->data_sock); return 0; }

    FILE *f = fopen(local, "wb");
    if (!f) { printf("Cannot create local file %s\n", local); closesocket(c->data_sock); return 0; }

    int bytes;
    long total = 0;
    while ((bytes = recv(c->data_sock, resp, sizeof(resp), 0)) > 0) {
        fwrite(resp, 1, bytes, f);
        total += bytes;
    }
    fclose(f);
    closesocket(c->data_sock);
    c->data_sock = INVALID_SOCKET;
    ftp_recv_response(c, resp, sizeof(resp)); // 226
    printf("Downloaded %ld bytes\n", total);
    return (total > 0);
}

void ftp_disconnect(FTPC *c) {
    char resp[FTP_BUFFER_SIZE];
    ftp_send_cmd(c, "QUIT");
    ftp_recv_response(c, resp, sizeof(resp));
    if (c->ctrl_sock != INVALID_SOCKET) closesocket(c->ctrl_sock);
    c->ctrl_sock = INVALID_SOCKET;
    WSACleanup();
}

// ---------------------- Interactive UI ----------------------
void http_menu() {
    int opt;
    char path[512];
    char body[1024];
    char username[64];
    char password[64];
    
    // Obtener credenciales HTTP una vez al entrar al menú
    printf("\n--- Credenciales HTTP ---\n");
    printf("Usuario (enter para 'admin'): ");
    fgets(username, sizeof(username), stdin); username[strcspn(username, "\n")] = 0;
    if (strlen(username) == 0) strcpy(username, "admin");
    
    printf("Password (enter para '1234'): ");
    fgets(password, sizeof(password), stdin); password[strcspn(password, "\n")] = 0;
    if (strlen(password) == 0) strcpy(password, "1234");
    
    printf("Credenciales configuradas: %s:%s\n", username, password);
    
    while (1) {
        printf("\n--- HTTP Menu ---\n");
        printf("1) GET\n2) HEAD\n3) POST to /api/echo\n4) Back\nOption: ");
        if (scanf("%d", &opt) != 1) { while(getchar()!='\n'); continue; }
        getchar();
        
        if (opt == 4) break;
        
        switch (opt) {
            case 1: // GET
                printf("Path (ej: /, /status): ");
                fgets(path, sizeof(path), stdin); path[strcspn(path, "\n")] = 0;
                send_http_request("GET", path, NULL, username, password);
                break;
                
            case 2: // HEAD
                printf("Path (ej: /, /status): ");
                fgets(path, sizeof(path), stdin); path[strcspn(path, "\n")] = 0;
                send_http_request("HEAD", path, NULL, username, password);
                break;
                
            case 3: // POST to /api/echo
                printf("Message to echo: ");
                fgets(body, sizeof(body), stdin); body[strcspn(body, "\n")] = 0;
                send_http_request("POST", "/api/echo", body, username, password);
                break;
        }
    }
}

void shared_files_menu() {
    FTPC client;
    memset(&client, 0, sizeof(client));
    client.ctrl_sock = INVALID_SOCKET;
    client.data_sock = INVALID_SOCKET;
    char username[64], password[64], local[512], remote[512];
    int opt;

    printf("\n=== Archivos Compartidos (FTP) ===\n");
    printf("Conexion a %s:%d\n", FTP_SERVER_IP, FTP_SERVER_PORT);
    printf("Usuario (enter para 'test'): ");
    fgets(username, sizeof(username), stdin); username[strcspn(username, "\n")] = 0;
    if (strlen(username)==0) strcpy(username, "test");
    printf("Password (enter para 'test123'): ");
    fgets(password, sizeof(password), stdin); password[strcspn(password, "\n")] = 0;
    if (strlen(password)==0) strcpy(password, "test123");

    if (!ftp_connect(&client, FTP_SERVER_IP, FTP_SERVER_PORT)) { 
        printf("No se pudo conectar al servidor FTP\n"); 
        return; 
    }
    if (!ftp_login(&client, username, password)) { 
        printf("Error de autenticacion FTP\n"); 
        ftp_disconnect(&client); 
        return; 
    }
    printf("FTP conectado y autenticado.\n");

    while (1) {
        printf("\n--- Archivos Compartidos ---\n");
        printf("1) Listar archivos\n2) Descargar archivo\n3) Subir archivo\n4) Volver\nOption: ");
        if (scanf("%d", &opt) != 1) { while(getchar()!='\n'); continue; }
        getchar();
        if (opt == 4) break;
        
        if (opt == 1) {
            // LIST: enter passive, connect data, LIST
            char resp[FTP_BUFFER_SIZE];
            if (!ftp_enter_passive(&client)) { printf("Error en modo pasivo\n"); continue; }
            if (!ftp_connect_data_socket(&client)) { printf("Error conectando socket de datos\n"); continue; }
            ftp_send_cmd(&client, "LIST");
            int code = ftp_recv_response(&client, resp, sizeof(resp));
            if (code != 150) { 
                printf("Comando LIST rechazado\n"); 
                closesocket(client.data_sock); 
                client.data_sock=INVALID_SOCKET; 
                continue; 
            }
            printf("\n--- Lista de archivos ---\n");
            int bytes;
            while ((bytes = recv(client.data_sock, resp, sizeof(resp)-1,0))>0) { 
                resp[bytes]=0; 
                printf("%s", resp); 
            }
            closesocket(client.data_sock); 
            client.data_sock=INVALID_SOCKET;
            ftp_recv_response(&client, resp, sizeof(resp));
        } 
        else if (opt == 2) {
            printf("Archivo remoto a descargar: ");
            fgets(remote, sizeof(remote), stdin); remote[strcspn(remote, "\n")] = 0;
            printf("Guardar como (local): ");
            fgets(local, sizeof(local), stdin); local[strcspn(local, "\n")] = 0;
            if (ftp_download_file(&client, remote, local)) {
                printf("Descarga completada\n");
            } else {
                printf("Error en la descarga\n");
            }
        } 
        else if (opt == 3) {
            printf("Archivo local a subir: ");
            fgets(local, sizeof(local), stdin); local[strcspn(local, "\n")] = 0;
            printf("Nombre en el servidor: ");
            fgets(remote, sizeof(remote), stdin); remote[strcspn(remote, "\n")] = 0;
            if (ftp_upload_file(&client, local, remote)) {
                printf("Subida completada\n");
            } else {
                printf("Error en la subida\n");
            }
        }
    }

    ftp_disconnect(&client);
    printf("Desconectado del servidor FTP.\n");
}

int main() {
    int mainopt;
    while (1) {
        printf("\n=== Cliente HTTP\n");
        printf("1) HTTP (GET, HEAD, POST)\n");
        printf("2) Archivos Compartidos\n");
        printf("3) Salir\n");
        printf("Option: ");
        if (scanf("%d", &mainopt) != 1) { while(getchar()!='\n'); continue; }
        getchar();
        if (mainopt == 1) http_menu();
        else if (mainopt == 2) shared_files_menu();
        else if (mainopt == 3) break;
    }
    printf("Hasta luego!\n");
    return 0;
}