// server.c - Servidor HTTP robusto con diagnóstico de errores
// Compilar: gcc server.c -o server.exe -lws2_32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 8192
#define WWWROOT "../wwwroot"
#define LOGFILE "../log/http.log"
#define USERS_FILE "../../config/http_users.txt"

CRITICAL_SECTION log_cs;
int total_requests = 0;
int total_ok = 0;
int total_errors = 0;

// --- Prototipos ---
DWORD WINAPI client_thread(LPVOID lpParam);
void serve_file(SOCKET client, const char *path, const char *ip, const char *method);
void handle_echo(SOCKET client, const char *body, const char *ip);
void handle_echo_info(SOCKET client, const char *ip);
void handle_status(SOCKET client, const char *ip);
void send_response(SOCKET client, int code, const char *msg, const char *type, const char *body);
void log_event(const char *ip, const char *method, const char *path, int status);
const char *get_mime_type(const char *filename);

// --- MIME ---
const char *get_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "text/plain";
    if (_stricmp(ext, ".html") == 0) return "text/html";
    if (_stricmp(ext, ".css") == 0) return "text/css";
    if (_stricmp(ext, ".js") == 0) return "application/javascript";
    if (_stricmp(ext, ".png") == 0) return "image/png";
    if (_stricmp(ext, ".jpg") == 0 || _stricmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(ext, ".gif") == 0) return "image/gif";
    if (_stricmp(ext, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

// --- Logger ---
void log_event(const char *ip, const char *method, const char *path, int status) {
    EnterCriticalSection(&log_cs);
    FILE *f = fopen(LOGFILE, "a");
    if (!f) {
        printf("No se pudo escribir en el log.\n");
        LeaveCriticalSection(&log_cs);
        return;
    }
    time_t now = time(NULL);
    struct tm t;
    localtime_s(&t, &now);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s %s %s -> %d\n",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec,
            ip, method, path, status);
    fclose(f);
    LeaveCriticalSection(&log_cs);
}

// --- Respuesta genérica ---
void send_response(SOCKET client, int code, const char *msg, const char *type, const char *body) {
    char header[1024];
    char date[64];
    time_t now = time(NULL);
    struct tm t;
    gmtime_s(&t, &now);
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &t);

    int body_len = body ? (int)strlen(body) : 0;
    int len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: RetoHTTP/1.1 (Windows)\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        code, msg, date, type, body_len);

    if (len > 0 && len < (int)sizeof(header)) {
        send(client, header, len, 0);
        if (body_len > 0 && body)
            send(client, body, body_len, 0);
    }
}

// --- /api/echo (POST) ---
void handle_echo(SOCKET client, const char *body, const char *ip) {
    char escaped_body[BUFFER_SIZE] = {0};
    if (body && strlen(body) > 0) {
        char *dst = escaped_body;
        const char *src = body;
        while (*src && (dst - escaped_body) < (int)sizeof(escaped_body) - 1) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            }
            *dst++ = *src++;
        }
        *dst = '\0';
    }

    char json[BUFFER_SIZE];
    snprintf(json, sizeof(json),
        "{\n  \"method\": \"POST\",\n  \"endpoint\": \"/api/echo\",\n  \"echo\": \"%s\"\n}",
        escaped_body[0] ? escaped_body : "");
    send_response(client, 200, "OK", "application/json", json);
    log_event(ip, "POST", "/api/echo", 200);
    total_ok++;
}

// --- /api/echo (GET) ---
void handle_echo_info(SOCKET client, const char *ip) {
    const char *msg =
        "<html><body><h1>/api/echo</h1>"
        "<p>Este endpoint acepta POST con texto plano.</p>"
        "<p>Ejemplo: <pre>curl -X POST http://localhost:8080/api/echo -d \"Hola\"</pre></p>"
        "</body></html>";
    send_response(client, 200, "OK", "text/html", msg);
    log_event(ip, "GET", "/api/echo", 200);
    total_ok++;
}

// --- /status ---
void handle_status(SOCKET client, const char *ip) {
    char html[1024];
    snprintf(html, sizeof(html),
        "<html><head><title>Status</title></head><body>"
        "<h1>Estado del Servidor</h1>"
        "<p>Total requests: %d</p>"
        "<p>Respuestas 200 OK: %d</p>"
        "<p>Errores: %d</p>"
        "</body></html>",
        total_requests, total_ok, total_errors);
    send_response(client, 200, "OK", "text/html", html);
    log_event(ip, "GET", "/status", 200);
    total_ok++;
}

// --- Archivos estáticos ---
void serve_file(SOCKET client, const char *path, const char *ip, const char *method) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client, 404, "Not Found", "text/html", msg);
        total_errors++;
        log_event(ip, method, path, 404);
        return;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *content = malloc(size + 1);
    if (!content) {
        printf("Error: memoria insuficiente al servir archivo %s\n", path);
        fclose(file);
        send_response(client, 500, "Internal Server Error", "text/html", "<h1>500 Internal Server Error</h1>");
        total_errors++;
        return;
    }

    size_t bytes_read = fread(content, 1, size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        free(content);
        send_response(client, 500, "Internal Server Error", "text/html", "<h1>500 Internal Server Error</h1>");
        total_errors++;
        return;
    }

    const char *mime_type = get_mime_type(path);
    if (strncmp(mime_type, "text/", 5) == 0 || 
        strcmp(mime_type, "application/javascript") == 0 ||
        strcmp(mime_type, "application/json") == 0) {
        content[size] = '\0';
        send_response(client, 200, "OK", mime_type, content);
    } else {
        char header[1024];
        char date[64];
        time_t now = time(NULL);
        struct tm t;
        gmtime_s(&t, &now);
        strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &t);
        
        int len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Date: %s\r\n"
            "Server: RetoHTTP/1.1 (Windows)\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n\r\n",
            date, mime_type, size);
            
        if (len > 0 && len < (int)sizeof(header)) {
            send(client, header, len, 0);
            if (strcmp(method, "HEAD") != 0)
                send(client, content, size, 0);
        }
    }

    log_event(ip, method, path, 200);
    total_ok++;
    free(content);
}

// --- Hilo del cliente ---
DWORD WINAPI client_thread(LPVOID lpParam) {
    SOCKET client = ((SOCKET*)lpParam)[0];
    struct sockaddr_in clientAddr = ((struct sockaddr_in*)(((char*)lpParam) + sizeof(SOCKET)))[0];
    free(lpParam);

    char ip[32];
    strcpy(ip, inet_ntoa(clientAddr.sin_addr));

    char buffer[BUFFER_SIZE];
    int bytes = recv(client, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        closesocket(client);
        return 0;
    }

    buffer[bytes] = '\0';
    total_requests++;

    char method[16], path[256];
    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        send_response(client, 400, "Bad Request", "text/html", "<h1>400 Bad Request</h1>");
        total_errors++;
        closesocket(client);
        return 0;
    }

    char *body = strstr(buffer, "\r\n\r\n");
    if (body) body += 4; else body = "";

    printf("%s %s %s\n", ip, method, path);

    if (strcmp(path, "/status") == 0) {
        handle_status(client, ip);
    } else if (strcmp(path, "/api/echo") == 0) {
        if (_stricmp(method, "POST") == 0) handle_echo(client, body, ip);
        else handle_echo_info(client, ip);
    } else if (_stricmp(method, "GET") == 0 || _stricmp(method, "HEAD") == 0) {
        if (strcmp(path, "/") == 0) strcpy(path, "/index.html");
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s%s", WWWROOT, path);
        serve_file(client, fullpath, ip, method);
    } else {
        send_response(client, 405, "Method Not Allowed", "text/html", "<h1>405 Method Not Allowed</h1>");
        log_event(ip, method, path, 405);
        total_errors++;
    }

    closesocket(client);
    return 0;
}

// --- MAIN ---
int main() {
    WSADATA wsa;
    SOCKET server;
    struct sockaddr_in serverAddr, clientAddr;
    int clientLen = sizeof(clientAddr);

    InitializeCriticalSection(&log_cs);

    // Crear carpetas necesarias
    system("mkdir \"../log\" 2>nul");
    system("mkdir \"../../config\" 2>nul");

    // Crear archivo de usuarios si no existe
    FILE *fcheck = fopen(USERS_FILE, "r");
    if (!fcheck) {
        FILE *fnew = fopen(USERS_FILE, "w");
        if (fnew) {
            fprintf(fnew, "admin:1234\n");
            fprintf(fnew, "test:test123\n");
            fprintf(fnew, "guest:guest\n");
            fclose(fnew);
            printf("Archivo creado: %s con usuarios por defecto.\n", USERS_FILE);
        } else {
            printf("No se pudo crear archivo de usuarios: %s\n", USERS_FILE);
        }
    } else {
        fclose(fcheck);
    }

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Error inicializando Winsock\n");
        getchar();
        return 1;
    }

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        printf("No se pudo crear socket (%d)\n", WSAGetLastError());
        WSACleanup();
        getchar();
        return 1;
    }

    int enable = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(server, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("bind fallo (%d)\n", WSAGetLastError());
        closesocket(server);
        WSACleanup();
        getchar();
        return 1;
    }

    if (listen(server, 10) == SOCKET_ERROR) {
        printf("listen fallo (%d)\n", WSAGetLastError());
        closesocket(server);
        WSACleanup();
        getchar();
        return 1;
    }

    printf("    Servidor HTTP escuchando en puerto %d...\n", PORT);
    printf("   Rutas: /, /status, /api/echo (GET y POST)\n");
    printf("   Directorio raiz: %s\n", WWWROOT);
    printf("   Archivo de usuarios: %s\n", USERS_FILE);
    printf("--------------------------------------------------\n");

    while (1) {
        SOCKET client = accept(server, (struct sockaddr*)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) continue;

        void *bundle = malloc(sizeof(SOCKET) + sizeof(struct sockaddr_in));
        memcpy(bundle, &client, sizeof(SOCKET));
        memcpy((char*)bundle + sizeof(SOCKET), &clientAddr, sizeof(struct sockaddr_in));

        DWORD tid;
        HANDLE h = CreateThread(NULL, 0, client_thread, bundle, 0, &tid);
        if (h) CloseHandle(h);
    }

    closesocket(server);
    WSACleanup();
    DeleteCriticalSection(&log_cs);
    return 0;
}
