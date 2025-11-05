// ftp_server_improved.c - Servidor FTP
// Compilar: gcc ftp_server.c -o ftp_server.exe -lws2_32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include <windows.h>
#include <direct.h>

#pragma comment(lib, "ws2_32.lib")

#define FTP_PORT 21 // Puerto estándar FTP
#define BUFFER_SIZE 8192
#define MAX_PATH_LEN 512
#define LOG_FILE "logs/ftp_server.log"
#define USERS_FILE "config/users.txt"

typedef struct {
    char username[64];
    char password[64];
    char home_dir[MAX_PATH_LEN];
} User;

typedef struct {
    SOCKET ctrl_sock;
    SOCKET data_sock;
    SOCKET pasv_sock;
    char current_dir[MAX_PATH_LEN]; // Ruta estilo Unix (ej: /test)
    char username[64];
    int logged_in;
    struct sockaddr_in client_addr;
    FILE* log_file;
} ClientSession;

// Funciones mejoradas
void log_message(FILE* log, const char* ip, int port, const char* cmd, const char* status, long duration, long size) {
    time_t now;
    struct tm* timeinfo;
    char timestamp[64];
    
    time(&now);
    timeinfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    fprintf(log, "[%s] IP=%s:%d CMD=%s STATUS=%s DURATION=%ldms SIZE=%ld\n",
            timestamp, ip, port, cmd, status, duration, size);
    fflush(log);
}

int load_users(User users[], int max_users) {
    FILE* f = fopen(USERS_FILE, "r");
    if (!f) {
        printf("Error: No se puede abrir %s\n", USERS_FILE);
        return 0;
    }
    
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && count < max_users) {
        // Formato: usuario:password:directorio_base (ej: ftp\test)
        char* username = strtok(line, ":");
        char* password = strtok(NULL, ":");
        char* homedir = strtok(NULL, "\n\r");
        
        if (username && password) {
            strncpy(users[count].username, username, sizeof(users[count].username) - 1);
            strncpy(users[count].password, password, sizeof(users[count].password) - 1);
            
            if (homedir && strlen(homedir) > 0) {
                strncpy(users[count].home_dir, homedir, sizeof(users[count].home_dir) - 1);
            } else {
                // Directorio por defecto
                snprintf(users[count].home_dir, sizeof(users[count].home_dir), "ftp\\%s", username);
            }
            count++;
        }
    }
    
    fclose(f);
    printf("Usuarios cargados: %d\n", count);
    return count;
}

void send_response(SOCKET sock, const char* code, const char* message) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s %s\r\n", code, message);
    send(sock, buffer, strlen(buffer), 0);
    printf(">> %s", buffer);
}

void handle_user(ClientSession* session, const char* username, User users[], int user_count) {
    strncpy(session->username, username, sizeof(session->username) - 1);
    send_response(session->ctrl_sock, "331", "Username OK, need password.");
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "USER", "331", 0, 0);
}

void handle_pass(ClientSession* session, const char* password, User users[], int user_count) {
    int authenticated = 0;
    User* user = NULL;
    
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, session->username) == 0 && 
            strcmp(users[i].password, password) == 0) {
            authenticated = 1;
            user = &users[i];
            break;
        }
    }
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    
    if (authenticated && user) {
        session->logged_in = 1;
        
        // Guardar ruta base estilo Unix (ej: /test)
        char unix_style_path[MAX_PATH_LEN];
        snprintf(unix_style_path, sizeof(unix_style_path), "/%s", user->username);
        strncpy(session->current_dir, unix_style_path, sizeof(session->current_dir) - 1);
        
        // Crear directorio si no existe
        CreateDirectoryA(user->home_dir, NULL);
        // Cambiar directorio de trabajo del HILO a la carpeta del usuario
        if (_chdir(user->home_dir) != 0) {
             send_response(session->ctrl_sock, "530", "Login failed. Cannot access home directory.");
             log_message(session->log_file, client_ip, client_port, "PASS", "530", 0, 0);
             session->logged_in = 0;
             return;
        }
        
        send_response(session->ctrl_sock, "230", "User logged in.");
        log_message(session->log_file, client_ip, client_port, "PASS", "230", 0, 0);
    } else {
        send_response(session->ctrl_sock, "530", "Login incorrect.");
        log_message(session->log_file, client_ip, client_port, "PASS", "530", 0, 0);
    }
}

void handle_pwd(ClientSession* session) {
    if (!session->logged_in) {
        send_response(session->ctrl_sock, "530", "Not logged in.");
        return;
    }
    
    char buffer[BUFFER_SIZE];
    // La ruta ya está guardada en formato Unix
    snprintf(buffer, sizeof(buffer), "\"%s\" is current directory.", session->current_dir);
    send_response(session->ctrl_sock, "257", buffer);
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "PWD", "257", 0, 0);
}

void handle_cwd(ClientSession* session, const char* path) {
    if (!session->logged_in) {
        send_response(session->ctrl_sock, "530", "Not logged in.");
        return;
    }

    // El servidor maneja el CWD real usando _chdir.
    // Esta función es compleja de implementar de forma segura (para evitar salir del home_dir).
    // Por ahora, solo implementamos ".." para subir un nivel (si no es la raíz)
    
    if (strcmp(path, "..") == 0) {
        // Obtener el directorio de trabajo actual del HILO
        char temp_path[MAX_PATH_LEN];
        _getcwd(temp_path, sizeof(temp_path));
        
        // (Simplificación) - No permitimos subir más allá del home_dir
        // Una implementación real necesitaría comparar contra la ruta base.
        // Por ahora, si _chdir("..") funciona, actualizamos la ruta interna.
        if (_chdir("..") == 0) {
            // Actualizar la ruta interna (session->current_dir)
            char* last_slash = strrchr(session->current_dir, '/');
            if (last_slash != NULL && last_slash != session->current_dir) { // No es la raíz
                *last_slash = '\0';
            } else if (last_slash == session->current_dir) { // Es la raíz
                session->current_dir[1] = '\0'; // Queda solo "/"
            }
            send_response(session->ctrl_sock, "250", "Directory changed.");
        } else {
            send_response(session->ctrl_sock, "550", "Cannot change directory (permission denied).");
        }
    } else if (path[0] == '/') {
        // Rutas absolutas (no seguras) - las rechazamos por ahora
         send_response(session->ctrl_sock, "550", "Absolute paths not supported.");
    } else {
        // Ruta relativa
        if (_chdir(path) == 0) {
            // Actualizar ruta interna
            if (strcmp(session->current_dir, "/") == 0) {
                snprintf(session->current_dir + 1, sizeof(session->current_dir) - 1, "%s", path);
            } else {
                strncat(session->current_dir, "/", sizeof(session->current_dir) - strlen(session->current_dir) - 1);
                strncat(session->current_dir, path, sizeof(session->current_dir) - strlen(session->current_dir) - 1);
            }
            send_response(session->ctrl_sock, "250", "Directory changed.");
        } else {
            send_response(session->ctrl_sock, "550", "Directory not found or access denied.");
        }
    }
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "CWD", "250", 0, 0);
}


void handle_pasv(ClientSession* session) {
    if (!session->logged_in) {
        send_response(session->ctrl_sock, "530", "Not logged in.");
        return;
    }
    
    // Crear socket pasivo
    session->pasv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (session->pasv_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Cannot open passive connection.");
        return;
    }
    
    struct sockaddr_in pasv_addr;
    memset(&pasv_addr, 0, sizeof(pasv_addr));
    pasv_addr.sin_family = AF_INET;
    pasv_addr.sin_addr.s_addr = INADDR_ANY;
    pasv_addr.sin_port = 0; // Puerto aleatorio
    
    if (bind(session->pasv_sock, (struct sockaddr*)&pasv_addr, sizeof(pasv_addr)) == SOCKET_ERROR) {
        closesocket(session->pasv_sock);
        send_response(session->ctrl_sock, "425", "Cannot open passive connection.");
        return;
    }
    
    if (listen(session->pasv_sock, 1) == SOCKET_ERROR) {
        closesocket(session->pasv_sock);
        send_response(session->ctrl_sock, "425", "Cannot open passive connection.");
        return;
    }
    
    // Obtener puerto asignado
    int addr_len = sizeof(pasv_addr);
    getsockname(session->pasv_sock, (struct sockaddr*)&pasv_addr, &addr_len);
    int port = ntohs(pasv_addr.sin_port);
    
    // Usar IP del servidor (localhost para pruebas)
    unsigned char ip_parts[4] = {127, 0, 0, 1};
    
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
             ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3], port / 256, port % 256);
    send_response(session->ctrl_sock, "227", buffer);
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "PASV", "227", 0, 0);
}

// --- INICIO DE CORRECCIÓN PARA BUG 550 ---
void handle_list(ClientSession* session) {
    if (!session->logged_in) {
        send_response(session->ctrl_sock, "530", "Not logged in.");
        return;
    }
    
    if (session->pasv_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Use PASV first.");
        return;
    }
    
    send_response(session->ctrl_sock, "150", "Opening ASCII mode data connection for file list");
    
    // Aceptar conexion de datos
    struct sockaddr_in data_client_addr;
    int addr_len = sizeof(data_client_addr);
    session->data_sock = accept(session->pasv_sock, (struct sockaddr*)&data_client_addr, &addr_len);
    
    if (session->data_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Cannot open data connection.");
        closesocket(session->pasv_sock);
        session->pasv_sock = INVALID_SOCKET;
        return;
    }
    
    // El servidor ya está en el directorio correcto (gracias a _chdir en handle_pass/handle_cwd)
    // Solo necesitamos listar el directorio actual "."
    char search_path[MAX_PATH_LEN];
    snprintf(search_path, sizeof(search_path), ".\\*");
    
    // Listar archivos
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    char buffer[BUFFER_SIZE];
    long total_size = 0;
    clock_t start = clock();
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // Saltar . y ..
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }
            
            // Formato detallado tipo UNIX
            char* type = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "d" : "-";
            char permissions[] = "rw-rw-rw-";
            LARGE_INTEGER file_size;
            file_size.LowPart = find_data.nFileSizeLow;
            file_size.HighPart = find_data.nFileSizeHigh;
            
            // Convertir tiempo
            SYSTEMTIME sys_time;
            FileTimeToSystemTime(&find_data.ftLastWriteTime, &sys_time);
            
            snprintf(buffer, sizeof(buffer), "%s%s 1 user group %10lld %04d-%02d-%02d %02d:%02d %s\r\n",
                    type, permissions, file_size.QuadPart,
                    sys_time.wYear, sys_time.wMonth, sys_time.wDay,
                    sys_time.wHour, sys_time.wMinute, find_data.cFileName);
            
            send(session->data_sock, buffer, strlen(buffer), 0);
            total_size += strlen(buffer);
            
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    } else {
        strcpy(buffer, "No files found\r\n");
        send(session->data_sock, buffer, strlen(buffer), 0);
        total_size = strlen(buffer);
    }
    
    long duration = (clock() - start) * 1000 / CLOCKS_PER_SEC;
    
    closesocket(session->data_sock);
    closesocket(session->pasv_sock);
    session->data_sock = INVALID_SOCKET;
    session->pasv_sock = INVALID_SOCKET;
    
    send_response(session->ctrl_sock, "226", "Directory send OK.");
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "LIST", "226", duration, total_size);
}

// --- INICIO DE CORRECCIÓN PARA BUG 550 ---
void handle_retr(ClientSession* session, const char* filename) {
    if (!session->logged_in) {
        send_response(session->ctrl_sock, "530", "Not logged in.");
        return;
    }
    
    if (session->pasv_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Use PASV first.");
        return;
    }
    
    // El CWD del servidor ya es correcto. Solo usamos el nombre del archivo.
    char full_path[MAX_PATH_LEN];
    strncpy(full_path, filename, sizeof(full_path) - 1);

    // Reemplazar / con \ para Windows (por si el cliente envía sub/archivo)
    for (char* p = full_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    
    FILE* file = fopen(full_path, "rb");
    if (!file) {
        send_response(session->ctrl_sock, "550", "File not found.");
        closesocket(session->pasv_sock);
        session->pasv_sock = INVALID_SOCKET;
        return;
    }
    
    send_response(session->ctrl_sock, "150", "Opening BINARY mode data connection");
    
    struct sockaddr_in data_client_addr;
    int addr_len = sizeof(data_client_addr);
    session->data_sock = accept(session->pasv_sock, (struct sockaddr*)&data_client_addr, &addr_len);
    
    if (session->data_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Cannot open data connection.");
        fclose(file);
        closesocket(session->pasv_sock);
        session->pasv_sock = INVALID_SOCKET;
        return;
    }
    
    clock_t start = clock();
    char buffer[BUFFER_SIZE];
    long total_size = 0;
    int bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(session->data_sock, buffer, bytes_read, 0);
        total_size += bytes_read;
    }
    
    long duration = (clock() - start) * 1000 / CLOCKS_PER_SEC;
    
    fclose(file);
    closesocket(session->data_sock);
    closesocket(session->pasv_sock);
    session->data_sock = INVALID_SOCKET;
    session->pasv_sock = INVALID_SOCKET;
    
    send_response(session->ctrl_sock, "226", "Transfer complete.");
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "RETR", "226", duration, total_size);
}

// --- INICIO DE CORRECCIÓN PARA BUG 550 ---
void handle_stor(ClientSession* session, const char* filename) {
    if (!session->logged_in) {
        send_response(session->ctrl_sock, "530", "Not logged in.");
        return;
    }
    
    if (session->pasv_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Use PASV first.");
        return;
    }
    
    // El CWD del servidor ya es correcto. Solo usamos el nombre del archivo.
    char full_path[MAX_PATH_LEN];
    strncpy(full_path, filename, sizeof(full_path) - 1);
    
    // Reemplazar / con \ para Windows (por si el cliente envía sub/archivo)
    for (char* p = full_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    
    FILE* file = fopen(full_path, "wb");
    if (!file) {
        send_response(session->ctrl_sock, "550", "Cannot create file.");
        closesocket(session->pasv_sock);
        session->pasv_sock = INVALID_SOCKET;
        return;
    }
    
    send_response(session->ctrl_sock, "150", "Opening BINARY mode data connection");
    
    struct sockaddr_in data_client_addr;
    int addr_len = sizeof(data_client_addr);
    session->data_sock = accept(session->pasv_sock, (struct sockaddr*)&data_client_addr, &addr_len);
    
    if (session->data_sock == INVALID_SOCKET) {
        send_response(session->ctrl_sock, "425", "Cannot open data connection.");
        fclose(file);
        closesocket(session->pasv_sock);
        session->pasv_sock = INVALID_SOCKET;
        return;
    }
    
    clock_t start = clock();
    char buffer[BUFFER_SIZE];
    long total_size = 0;
    int bytes_recv;
    
    while ((bytes_recv = recv(session->data_sock, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytes_recv, file);
        total_size += bytes_recv;
    }
    
    long duration = (clock() - start) * 1000 / CLOCKS_PER_SEC;
    
    fclose(file);
    closesocket(session->data_sock);
    closesocket(session->pasv_sock);
    session->data_sock = INVALID_SOCKET;
    session->pasv_sock = INVALID_SOCKET;
    
    send_response(session->ctrl_sock, "226", "Transfer complete.");
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "STOR", "226", duration, total_size);
}
// --- FIN DE CORRECCIÓN ---


void handle_quit(ClientSession* session) {
    send_response(session->ctrl_sock, "221", "Goodbye.");
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    log_message(session->log_file, client_ip, client_port, "QUIT", "221", 0, 0);
}

DWORD WINAPI client_handler(LPVOID param) {
    ClientSession* session = (ClientSession*)param;
    char buffer[BUFFER_SIZE];
    int bytes_recv;
    
    User users[100];
    int user_count = load_users(users, 100);
    
    // Guardar directorio raíz del proyecto
    char root_dir[MAX_PATH_LEN];
    _getcwd(root_dir, sizeof(root_dir));
    
    // Inicializar directorio para el log
    strcpy(session->current_dir, "/");
    
    char client_ip[16];
    strcpy(client_ip, inet_ntoa(session->client_addr.sin_addr));
    int client_port = ntohs(session->client_addr.sin_port);
    
    printf("Cliente conectado: %s:%d\n", client_ip, client_port);
    send_response(session->ctrl_sock, "220", "FTP Server Ready.");
    
    while ((bytes_recv = recv(session->ctrl_sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_recv] = '\0';
        printf("<< %s", buffer);
        
        // Remover \r\n
        buffer[strcspn(buffer, "\r\n")] = '\0';
        
        char cmd[32];
        char arg[BUFFER_SIZE] = "";
        
        // Parsear comando y argumento
        if (sscanf(buffer, "%31s %[^\r\n]", cmd, arg) < 1) {
            continue;
        }
        
        // Convertir a mayúsculas para comparación
        for (char* p = cmd; *p; p++) {
            *p = toupper(*p);
        }
        
        if (strcmp(cmd, "USER") == 0) {
            handle_user(session, arg, users, user_count);
        } else if (strcmp(cmd, "PASS") == 0) {
            handle_pass(session, arg, users, user_count);
        } else if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
            handle_pwd(session);
        } else if (strcmp(cmd, "CWD") == 0) {
            handle_cwd(session, arg);
        } else if (strcmp(cmd, "PASV") == 0) {
            handle_pasv(session);
        } else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
            handle_list(session);
        } else if (strcmp(cmd, "RETR") == 0) {
            handle_retr(session, arg);
        } else if (strcmp(cmd, "STOR") == 0) {
            handle_stor(session, arg);
        } else if (strcmp(cmd, "QUIT") == 0) {
            handle_quit(session);
            break;
        } else if (strcmp(cmd, "SYST") == 0) {
            send_response(session->ctrl_sock, "215", "UNIX Type: L8");
        } else if (strcmp(cmd, "TYPE") == 0) {
            send_response(session->ctrl_sock, "200", "Type set to I");
        } else if (strcmp(cmd, "NOOP") == 0) {
            send_response(session->ctrl_sock, "200", "OK");
        } else {
            send_response(session->ctrl_sock, "500", "Unknown command.");
        }
    }
    
    closesocket(session->ctrl_sock);
    if (session->data_sock != INVALID_SOCKET) closesocket(session->data_sock);
    if (session->pasv_sock != INVALID_SOCKET) closesocket(session->pasv_sock);
    
    // Regresar al directorio raíz del proyecto antes de que el hilo muera
    _chdir(root_dir); 
    free(session);
    
    return 0;
}

int main() {
    // Configurar consola para UTF-8 (acentos)
    SetConsoleOutputCP(65001);

    WSADATA wsa;
    SOCKET server_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    
    printf("=== Servidor FTP Mejorado ===\n");
    
    // Crear directorios necesarios
    CreateDirectoryA("logs", NULL);
    CreateDirectoryA("config", NULL);
    CreateDirectoryA("ftp", NULL);
    
    // Crear subdirectorios de usuarios base
    CreateDirectoryA("ftp\\admin", NULL);
    CreateDirectoryA("ftp\\user", NULL);
    CreateDirectoryA("ftp\\test", NULL);

    // Crear archivo de usuarios si no existe
    FILE* users_file = fopen(USERS_FILE, "r");
    if (!users_file) {
        users_file = fopen(USERS_FILE, "w");
        fprintf(users_file, "admin:admin123:ftp\\admin\n");
        fprintf(users_file, "user:password:ftp\\user\n");
        fprintf(users_file, "test:test123:ftp\\test\n");
        fclose(users_file);
        printf("Archivo de usuarios creado: %s\n", USERS_FILE);
    } else {
        fclose(users_file);
    }
    
    printf("Inicializando Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Error al inicializar Winsock: %d\n", WSAGetLastError());
        return 1;
    }
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        printf("Error al crear socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    // Permitir reuso de direccion
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(FTP_PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Error en bind (puerto %d): %d\n", FTP_PORT, WSAGetLastError());
        printf("Sugerencia: Ejecutar como Administrador o cambiar puerto\n");
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }
    
    if (listen(server_sock, 10) == SOCKET_ERROR) {
        printf("Error en listen: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }
    
    FILE* log_file = fopen(LOG_FILE, "a");
    printf("Servidor FTP iniciado en puerto %d\n", FTP_PORT);
    printf("Esperando conexiones...\n\n");
    
    while (1) {
        SOCKET client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock == INVALID_SOCKET) {
            printf("Error al aceptar conexión: %d\n", WSAGetLastError());
            continue;
        }
        
        ClientSession* session = (ClientSession*)malloc(sizeof(ClientSession));
        session->ctrl_sock = client_sock;
        session->data_sock = INVALID_SOCKET;
        session->pasv_sock = INVALID_SOCKET;
        session->logged_in = 0;
        session->client_addr = client_addr;
        session->log_file = log_file;
        
        CreateThread(NULL, 0, client_handler, session, 0, NULL);
    }
    
    fclose(log_file);
    closesocket(server_sock);
    WSACleanup();
    return 0;
}