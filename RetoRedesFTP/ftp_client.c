// ftp_client_improved.c - Cliente FTP
// Compilar: gcc ftp_client.c -o ftp_client.exe -lws2_32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 8192
#define FTP_PORT 21 // Puerto estándar FTP

typedef struct {
    SOCKET ctrl_sock;
    SOCKET data_sock;
    char server_ip[64];
    int server_port;
    int pasv_port;
    char pasv_ip[64];
} FTPClient;

void send_command(FTPClient* client, const char* command) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s\r\n", command);
    send(client->ctrl_sock, buffer, strlen(buffer), 0);
    printf(">> %s", buffer);
}

int recv_response(FTPClient* client, char* response, int max_len) {
    int bytes = recv(client->ctrl_sock, response, max_len - 1, 0);
    if (bytes > 0) {
        response[bytes] = '\0';
        printf("<< %s", response);
        
        // Extraer código de respuesta
        char code[4];
        strncpy(code, response, 3);
        code[3] = '\0';
        return atoi(code);
    }
    return 0;
}

int connect_ftp(FTPClient* client, const char* server, int port) {
    WSADATA wsa;
    struct sockaddr_in addr;
    char response[BUFFER_SIZE];
    
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Error inicializando Winsock: %d\n", WSAGetLastError());
        return 0;
    }
    
    client->ctrl_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->ctrl_sock == INVALID_SOCKET) {
        printf("Error creando socket: %d\n", WSAGetLastError());
        return 0;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(server);
    addr.sin_port = htons(port);
    
    printf("Conectando a %s:%d...\n", server, port);
    if (connect(client->ctrl_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Error conectando: %d\n", WSAGetLastError());
        closesocket(client->ctrl_sock);
        return 0;
    }
    
    strncpy(client->server_ip, server, sizeof(client->server_ip) - 1);
    client->server_port = port;
    
    // Recibir mensaje de bienvenida
    int code = recv_response(client, response, sizeof(response));
    return (code == 220);
}

int login_ftp(FTPClient* client, const char* username, const char* password) {
    char response[BUFFER_SIZE];
    char command[256];
    
    snprintf(command, sizeof(command), "USER %s", username);
    send_command(client, command);
    int code = recv_response(client, response, sizeof(response));
    
    if (code != 331) {
        printf("Error: Usuario no aceptado\n");
        return 0;
    }
    
    snprintf(command, sizeof(command), "PASS %s", password);
    send_command(client, command);
    code = recv_response(client, response, sizeof(response));
    
    return (code == 230);
}

int parse_pasv_response(const char* response, char* ip, int* port) {
    // Formato: 227 Entering Passive Mode (127,0,0,1,195,143)
    const char* start = strchr(response, '(');
    if (!start) return 0;
    
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(start, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return 0;
    }
    
    snprintf(ip, 16, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;
    return 1;
}

int enter_passive_mode(FTPClient* client) {
    char response[BUFFER_SIZE];
    
    send_command(client, "PASV");
    int code = recv_response(client, response, sizeof(response));
    
    if (code != 227) {
        printf("Error: No se pudo entrar en modo pasivo\n");
        return 0;
    }
    
    if (!parse_pasv_response(response, client->pasv_ip, &client->pasv_port)) {
        printf("Error: No se pudo parsear respuesta PASV\n");
        return 0;
    }
    
    printf("Modo pasivo: %s:%d\n", client->pasv_ip, client->pasv_port);
    return 1;
}

int connect_data_socket(FTPClient* client) {
    struct sockaddr_in addr;
    
    client->data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->data_sock == INVALID_SOCKET) {
        printf("Error creando socket de datos: %d\n", WSAGetLastError());
        return 0;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(client->pasv_ip);
    addr.sin_port = htons(client->pasv_port);
    
    printf("Conectando canal de datos a %s:%d...\n", client->pasv_ip, client->pasv_port);
    if (connect(client->data_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Error conectando canal de datos: %d\n", WSAGetLastError());
        closesocket(client->data_sock);
        client->data_sock = INVALID_SOCKET;
        return 0;
    }
    
    return 1;
}

int list_directory(FTPClient* client) {
    char response[BUFFER_SIZE];
    
    if (!enter_passive_mode(client)) {
        return 0;
    }
    
    if (!connect_data_socket(client)) {
        return 0;
    }
    
    send_command(client, "LIST");
    int code = recv_response(client, response, sizeof(response));
    
    if (code != 150) {
        closesocket(client->data_sock);
        client->data_sock = INVALID_SOCKET;
        return 0;
    }
    
    printf("\n--- Listado de archivos ---\n");
    int bytes;
    long total_bytes = 0;
    clock_t start = clock();
    
    while ((bytes = recv(client->data_sock, response, sizeof(response) - 1, 0)) > 0) {
        response[bytes] = '\0';
        printf("%s", response);
        total_bytes += bytes;
    }
    
    double duration = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("---------------------------\n");
    printf("Total: %ld bytes (%.2f segundos)\n\n", total_bytes, duration);
    
    closesocket(client->data_sock);
    client->data_sock = INVALID_SOCKET;
    
    recv_response(client, response, sizeof(response)); // 226
    
    return 1;
}

int download_file(FTPClient* client, const char* remote_file, const char* local_file) {
    char response[BUFFER_SIZE];
    char command[256];
    
    if (!enter_passive_mode(client)) {
        return 0;
    }
    
    if (!connect_data_socket(client)) {
        return 0;
    }
    
    snprintf(command, sizeof(command), "RETR %s", remote_file);
    send_command(client, command);
    int code = recv_response(client, response, sizeof(response));
    
    if (code != 150) {
        printf("Error: archivo no encontrado o no disponible\n");
        closesocket(client->data_sock);
        client->data_sock = INVALID_SOCKET;
        return 0;
    }
    
    FILE* file = fopen(local_file, "wb");
    if (!file) {
        printf("Error creando archivo local: %s\n", local_file);
        closesocket(client->data_sock);
        client->data_sock = INVALID_SOCKET;
        return 0;
    }
    
    printf("Descargando %s -> %s...\n", remote_file, local_file);
    clock_t start = clock();
    long total_bytes = 0;
    int bytes;
    
    while ((bytes = recv(client->data_sock, response, sizeof(response), 0)) > 0) {
        fwrite(response, 1, bytes, file);
        total_bytes += bytes;
        printf("\rDescargados: %ld bytes", total_bytes);
    }
    
    double duration = (double)(clock() - start) / CLOCKS_PER_SEC;
    fclose(file);
    closesocket(client->data_sock);
    client->data_sock = INVALID_SOCKET;
    
    printf("\n");
    recv_response(client, response, sizeof(response)); // 226
    
    if (total_bytes > 0) {
        printf("Descarga completada: %ld bytes en %.2f segundos (%.2f KB/s)\n",
               total_bytes, duration, (total_bytes / 1024.0) / duration);
        return 1;
    } else {
        printf("Error: archivo vacío o no se pudo descargar\n");
        return 0;
    }
}

int upload_file(FTPClient* client, const char* local_file, const char* remote_file) {
    char response[BUFFER_SIZE];
    char command[256];
    
    FILE* file = fopen(local_file, "rb");
    if (!file) {
        printf("Error: archivo local no encontrado: %s\n", local_file);
        return 0;
    }
    
    if (!enter_passive_mode(client)) {
        fclose(file);
        return 0;
    }
    
    if (!connect_data_socket(client)) {
        fclose(file);
        return 0;
    }
    
    snprintf(command, sizeof(command), "STOR %s", remote_file);
    send_command(client, command);
    int code = recv_response(client, response, sizeof(response));
    
    if (code != 150) {
        printf("Error preparando carga de archivo\n");
        fclose(file);
        closesocket(client->data_sock);
        client->data_sock = INVALID_SOCKET;
        return 0;
    }
    
    printf("Subiendo %s -> %s...\n", local_file, remote_file);
    clock_t start = clock();
    long total_bytes = 0;
    int bytes;
    
    while ((bytes = fread(response, 1, sizeof(response), file)) > 0) {
        int sent = send(client->data_sock, response, bytes, 0);
        if (sent > 0) {
            total_bytes += sent;
            printf("\rEnviados: %ld bytes", total_bytes);
        } else {
            printf("\nError enviando datos\n");
            break;
        }
    }
    
    double duration = (double)(clock() - start) / CLOCKS_PER_SEC;
    fclose(file);
    closesocket(client->data_sock);
    client->data_sock = INVALID_SOCKET;
    
    printf("\n");
    recv_response(client, response, sizeof(response)); // 226
    
    if (total_bytes > 0) {
        printf("Carga completada: %ld bytes en %.2f segundos (%.2f KB/s)\n",
               total_bytes, duration, (total_bytes / 1024.0) / duration);
        return 1;
    } else {
        printf("Error: no se pudo subir el archivo\n");
        return 0;
    }
}

void disconnect_ftp(FTPClient* client) {
    char response[BUFFER_SIZE];
    send_command(client, "QUIT");
    recv_response(client, response, sizeof(response));
    
    if (client->data_sock != INVALID_SOCKET) {
        closesocket(client->data_sock);
    }
    closesocket(client->ctrl_sock);
    WSACleanup();
}

void print_menu() {
    printf("\n=== Cliente FTP ===\n");
    printf("1. Listar archivos (LIST)\n");
    printf("2. Descargar archivo (RETR)\n");
    printf("3. Subir archivo (STOR)\n");
    printf("4. Cambiar directorio (CWD)\n");
    printf("5. Mostrar directorio actual (PWD)\n");
    printf("6. Información del sistema (SYST)\n");
    printf("7. Salir (QUIT)\n");
    printf("Opción: ");
}

void get_input(const char* prompt, char* buffer, size_t size) {
    printf("%s", prompt);
    fgets(buffer, size, stdin);
    buffer[strcspn(buffer, "\n")] = 0;
}

int main() {
    // Configurar consola para UTF-8 (acentos)
    SetConsoleOutputCP(65001);

    FTPClient client;
    char server[64];
    char username[64];
    char password[64];
    
    printf("=== Cliente FTP - Versión Mejorada ===\n\n");
    
    // Se elimina la solicitud de IP y se fija a 127.0.0.1
    strcpy(server, "127.0.0.1");
    printf("Servidor fijado a: %s\n", server);
    
    get_input("Usuario (por defecto test): ", username, sizeof(username));
    if (strlen(username) == 0) {
        strcpy(username, "test");
    }
    
    get_input("Password (por defecto test123): ", password, sizeof(password));
    if (strlen(password) == 0) {
        strcpy(password, "test123");
    }
    
    printf("\nConectando a %s...\n", server);
    
    if (!connect_ftp(&client, server, FTP_PORT)) {
        printf("Error conectando al servidor\n");
        return 1;
    }
    
    printf("\nConexión establecida\n\n");
    
    if (!login_ftp(&client, username, password)) {
        printf("Error de autenticación\n");
        disconnect_ftp(&client);
        return 1;
    }
    
    printf("\nAutenticación exitosa\n\n");
    
    // Modo interactivo
    int option;
    char response[BUFFER_SIZE];
    char local_file[256], remote_file[256], dir_path[256];
    
    while (1) {
        print_menu();
        if (scanf("%d", &option) != 1) {
            // Limpiar buffer en caso de error
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Opción inválida\n");
            continue;
        }
        getchar(); // Consumir newline
        
        switch (option) {
            case 1: // LIST
                printf("\nEjecutando LIST...\n");
                if (list_directory(&client)) {
                    printf("LIST ejecutado correctamente\n");
                } else {
                    printf("Error en LIST\n");
                }
                break;
                
            case 2: // RETR
                get_input("Archivo remoto a descargar: ", remote_file, sizeof(remote_file));
                get_input("Guardar como (local): ", local_file, sizeof(local_file));
                
                if (strlen(remote_file) == 0 || strlen(local_file) == 0) {
                    printf("Error: nombres de archivo vacíos\n");
                    break;
                }
                
                printf("\nEjecutando RETR...\n");
                if (download_file(&client, remote_file, local_file)) {
                    printf("RETR ejecutado correctamente\n");
                } else {
                    printf("Error en RETR\n");
                }
                break;
                
            case 3: // STOR
                get_input("Archivo local a subir: ", local_file, sizeof(local_file));
                get_input("Nombre en servidor (remoto): ", remote_file, sizeof(remote_file));
                
                if (strlen(local_file) == 0 || strlen(remote_file) == 0) {
                    printf("Error: nombres de archivo vacíos\n");
                    break;
                }
                
                printf("\nEjecutando STOR...\n");
                if (upload_file(&client, local_file, remote_file)) {
                    printf("STOR ejecutado correctamente\n");
                } else {
                    printf("Error en STOR\n");
                }
                break;
                
            case 4: // CWD
                get_input("Directorio: ", dir_path, sizeof(dir_path));
                
                if (strlen(dir_path) == 0) {
                    printf("Error: directorio vacío\n");
                    break;
                }
                
                sprintf(response, "CWD %s", dir_path);
                send_command(&client, response);
                recv_response(&client, response, sizeof(response));
                break;
                
            case 5: // PWD
                send_command(&client, "PWD");
                recv_response(&client, response, sizeof(response));
                break;
                
            case 6: // SYST
                send_command(&client, "SYST");
                recv_response(&client, response, sizeof(response));
                break;
                
            case 7: // QUIT
                printf("\nCerrando conexión...\n");
                disconnect_ftp(&client);
                printf("Desconectado.\n");
                return 0;
                
            default:
                printf("Opción inválida\n");
        }
        
        printf("\nPresione ENTER para continuar...");
        getchar();
    }
    
    return 0;
}