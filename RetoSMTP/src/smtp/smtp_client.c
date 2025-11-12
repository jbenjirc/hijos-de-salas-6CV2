/* smtp_client.c - Cliente SMTP simple (IPv4) para Windows/MinGW/MSVC
   Envía texto y opcionalmente 1 adjunto (MIME + base64) a un servidor SMTP (localhost o remoto)
   Uso: smtp_client.exe <host> <puerto> <remitente> <destinatario> <asunto> [ruta_adjunto]
   Compilar (MinGW): gcc -Wall -o smtp_client.exe .\src\smtp\smtp_client.c -lws2_32
   Compilar (MSVC) : cl /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:smtp_client.exe .\src\smtp\smtp_client.c ws2_32.lib
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

#define SEND_BUFSZ 8192

static int send_all(SOCKET s, const char* data) {
    size_t len = strlen(data);
    const char* p = data;
    while (len > 0) {
        int n = send(s, p, (int)len, 0);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

static int recv_line(SOCKET s, char* out, int outsz) {
    int idx = 0;
    while (idx < outsz - 1) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return r;
        out[idx++] = c;
        if (idx >= 2 && out[idx-2] == '\r' && out[idx-1] == '\n') {
            out[idx] = '\0';
            return idx;
        }
    }
    out[outsz-1] = '\0';
    return idx;
}

// Base64 con CRLF cada 76 caracteres
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const unsigned char* data, size_t len) {
    size_t outlen = 4 * ((len + 2) / 3);
    size_t with_breaks = outlen + (outlen / 76 + 4) * 2 + 4;
    char* out = (char*)malloc(with_breaks + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0, col = 0;
    while (i < len) {
        unsigned a = data[i++];
        unsigned b = (i < len) ? data[i++] : 0;
        unsigned c = (i < len) ? data[i++] : 0;
        unsigned triple = (a << 16) | (b << 8) | c;

        out[j++] = B64[(triple >> 18) & 0x3F];
        out[j++] = B64[(triple >> 12) & 0x3F];
        out[j++] = (i - 1 > len) ? '=' : B64[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : B64[triple & 0x3F];

        // padding correcto al final
        if (i >= len) {
            size_t rem = len % 3;
            if (rem == 1) { out[j-1] = '='; out[j-2] = '='; }
            else if (rem == 2) { out[j-1] = '='; }
        }

        col += 4;
        if (col >= 76) { out[j++] = '\r'; out[j++] = '\n'; col = 0; }
    }
    if (col != 0) { out[j++] = '\r'; out[j++] = '\n'; }
    out[j] = '\0';
    return out;
}

static char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static void now_rfc2822(char* out, size_t outsz) {
    time_t t = time(NULL);
    struct tm* tmv = localtime(&t);
    static const char* dayname[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* monname[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

    TIME_ZONE_INFORMATION tz; DWORD tzres = GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias;
    if (tzres == TIME_ZONE_ID_STANDARD) bias += tz.StandardBias;
    if (tzres == TIME_ZONE_ID_DAYLIGHT) bias += tz.DaylightBias;
    int sign = (bias > 0) ? '-' : '+';
    int hh = abs(bias) / 60;
    int mm = abs(bias) % 60;

    snprintf(out, outsz, "%s, %02d %s %04d %02d:%02d:%02d %c%02d%02d",
        dayname[tmv->tm_wday], tmv->tm_mday, monname[tmv->tm_mon],
        tmv->tm_year + 1900, tmv->tm_hour, tmv->tm_min, tmv->tm_sec, sign, hh, mm);
}

// Conexión IPv4 sin getaddrinfo (compatible con MinGW)
static SOCKET connect_ipv4(const char* host, int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    struct hostent* he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
    addr.sin_port = htons((u_short)port);

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

int main(int argc, char** argv) {
    if (argc < 6) {
        fprintf(stderr, "Uso:\n  %s <host> <puerto> <remitente> <destinatario> <asunto> [ruta_adjunto]\n", argv[0]);
        fprintf(stderr, "Ejemplo:\n  %s 127.0.0.1 2525 \"beto@local\" \"dest@local\" \"Prueba\" \"C:\\\\ruta\\\\archivo.txt\"\n", argv[0]);
        return 1;
    }
    const char* host   = argv[1];
    int         port   = atoi(argv[2]);
    const char* from   = argv[3];
    const char* rcpt   = argv[4];
    const char* subject= argv[5];
    const char* attach_path = (argc >= 7) ? argv[6] : NULL;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }

    SOCKET s = connect_ipv4(host, port);
    if (s == INVALID_SOCKET) { fprintf(stderr, "connect failed (%d)\n", WSAGetLastError()); WSACleanup(); return 1; }

    char line[2048], tmp[SEND_BUFSZ];

    // 220 banner
    recv_line(s, line, sizeof(line));

    // EHLO
    send_all(s, "EHLO localhost\r\n");
    // Consumir 250-... hasta la última 250 ...
    do {
        if (recv_line(s, line, sizeof(line)) <= 0) break;
    } while (!strncmp(line, "250-", 4));

    // MAIL FROM
    snprintf(tmp, sizeof(tmp), "MAIL FROM:<%s>\r\n", from);
    send_all(s, tmp); recv_line(s, line, sizeof(line));

    // RCPT TO
    snprintf(tmp, sizeof(tmp), "RCPT TO:<%s>\r\n", rcpt);
    send_all(s, tmp); recv_line(s, line, sizeof(line));

    // DATA
    send_all(s, "DATA\r\n"); recv_line(s, line, sizeof(line)); // 354

    char datebuf[128]; now_rfc2822(datebuf, sizeof(datebuf));
    char boundary[64]; snprintf(boundary, sizeof(boundary), "----=_EduSMTP_%u", (unsigned)GetTickCount());

    if (!attach_path) {
        // Texto simple
        snprintf(tmp, sizeof(tmp),
            "From: %s\r\n"
            "To: %s\r\n"
            "Subject: %s\r\n"
            "Date: %s\r\n"
            "MIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=\"utf-8\"\r\n"
            "\r\n"
            "Hola,\r\nEste es un mensaje de prueba enviado por el cliente SMTP educativo.\r\n"
            ".\r\n",
            from, rcpt, subject, datebuf);
        send_all(s, tmp);
    } else {
        // Leer adjunto y codificar
        size_t raw_len = 0;
        char* raw = read_file(attach_path, &raw_len);
        if (!raw) {
            fprintf(stderr, "No se pudo leer el adjunto: %s\n", attach_path);
            closesocket(s); WSACleanup(); return 1;
        }
        char* b64 = base64_encode((unsigned char*)raw, raw_len);
        free(raw);
        if (!b64) {
            fprintf(stderr, "Fallo base64\n");
            closesocket(s); WSACleanup(); return 1;
        }

        const char* fname = attach_path;
        for (const char* p = attach_path; *p; ++p) if (*p=='\\' || *p=='/') fname = p+1;

        // Cabecera multipart
        char header[SEND_BUFSZ];
        snprintf(header, sizeof(header),
            "From: %s\r\n"
            "To: %s\r\n"
            "Subject: %s\r\n"
            "Date: %s\r\n"
            "MIME-Version: 1.0\r\n"
            "Content-Type: multipart/mixed; boundary=\"%s\"\r\n"
            "\r\n"
            "This is a multipart message in MIME format.\r\n"
            "--%s\r\n"
            "Content-Type: text/plain; charset=\"utf-8\"\r\n"
            "Content-Transfer-Encoding: 7bit\r\n"
            "\r\n"
            "Hola,\r\nAdjunto encontrarás el archivo solicitado.\r\n"
            "\r\n"
            "--%s\r\n"
            "Content-Type: application/octet-stream; name=\"%s\"\r\n"
            "Content-Transfer-Encoding: base64\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "\r\n",
            from, rcpt, subject, datebuf, boundary, boundary, boundary, fname, fname);

        send_all(s, header);
        send_all(s, b64);
        free(b64);

        snprintf(tmp, sizeof(tmp),
            "\r\n--%s--\r\n"
            ".\r\n",
            boundary);
        send_all(s, tmp);
    }

    // Respuesta a DATA
    recv_line(s, line, sizeof(line));

    // QUIT
    send_all(s, "QUIT\r\n"); recv_line(s, line, sizeof(line));

    closesocket(s);
    WSACleanup();
    printf("Mensaje enviado.\n");
    return 0;
}
