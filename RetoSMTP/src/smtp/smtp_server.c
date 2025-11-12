/* smtp_server.c - Servidor SMTP educativo (RFC 5321 básico) para Windows/MinGW/MSVC
   Acepta: HELO/EHLO, MAIL FROM, RCPT TO, DATA, QUIT
   Guarda mensajes .eml en .\smtp\inbox\ (headers + cuerpo)
   Compilar (MinGW): gcc -Wall -o smtp_server.exe .\src\smtp\smtp_server.c -lws2_32
   Compilar (MSVC) : cl /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:smtp_server.exe .\src\smtp\smtp_server.c ws2_32.lib
*/

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <direct.h>     // _mkdir
#include <stdlib.h>
#include <stdarg.h>

#define DEFAULT_PORT 2525
#define RECV_BUFSZ   4096
#define LINE_BUFSZ   2048
#define INBOX_DIR    ".\\smtp\\inbox\\"

typedef struct {
    char helo[256];
    char mail_from[512];
    char rcpt_to[20][512];
    int  rcpt_count;
    int  have_mail_from;
    int  have_rcpt;
    int  in_data;
} smtp_state;

static void ensure_dirs(void) {
    _mkdir(".\\smtp");
    _mkdir(".\\smtp\\inbox");
}

static int send_line(SOCKET s, const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (strlen(buf) < 2 || !(buf[strlen(buf)-2] == '\r' && buf[strlen(buf)-1] == '\n')) {
        strncat(buf, "\r\n", sizeof(buf) - strlen(buf) - 1);
    }
    return send(s, buf, (int)strlen(buf), 0);
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

static void trim_crlf(char* s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
}

static void reset_session(smtp_state* st) {
    memset(st, 0, sizeof(*st));
}

static void rfc5321_unstuff_dot(char* line) {
    if (line[0] == '.' && line[1] == '.') {
        memmove(line, line+1, strlen(line));
    }
}

static void localtime_safe(const time_t* t, struct tm* out) {
#if defined(_MSC_VER)
    localtime_s(out, t);
#else
    struct tm* p = localtime(t);
    if (p) *out = *p; else memset(out, 0, sizeof(*out));
#endif
}

static void now_rfc2822(char* out, size_t outsz) {
    time_t t = time(NULL);
    struct tm tmv; localtime_safe(&t, &tmv);
    static const char dayname[][4] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char monname[][4] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

    TIME_ZONE_INFORMATION tz; DWORD tzres = GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias;
    if (tzres == TIME_ZONE_ID_STANDARD) bias += tz.StandardBias;
    if (tzres == TIME_ZONE_ID_DAYLIGHT) bias += tz.DaylightBias;
    int sign = (bias > 0) ? '-' : '+';
    int hh = abs(bias) / 60;
    int mm = abs(bias) % 60;

    snprintf(out, outsz, "%s, %02d %s %04d %02d:%02d:%02d %c%02d%02d",
        dayname[tmv.tm_wday], tmv.tm_mday, monname[tmv.tm_mon],
        tmv.tm_year + 1900, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, sign, hh, mm);
}

static int looks_like_headers(const char* s) {
    // Heurística: detecta si el DATA empieza con headers comunes
    return (!_strnicmp(s, "From:", 5) ||
            !_strnicmp(s, "To:", 3) ||
            !_strnicmp(s, "Subject:", 8) ||
            !_strnicmp(s, "MIME-Version:", 13) ||
            !_strnicmp(s, "Content-Type:", 13) ||
            !_strnicmp(s, "Date:", 5) ||
            !_strnicmp(s, "Received:", 9));
}

static void save_eml(const smtp_state* st, const char* data_buf) {
    ensure_dirs();

    char ts[32];
    time_t t = time(NULL);
    struct tm tmv; localtime_safe(&t, &tmv);
    snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d",
        tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), INBOX_DIR "%s_%u.eml", ts, (unsigned)GetTickCount());

    FILE* f = fopen(path, "wb");
    if (!f) return;

    char datebuf[128];
    now_rfc2822(datebuf, sizeof(datebuf));

    // Solo agregamos un Received: arriba. Luego pegamos íntegro lo que envió el cliente.
    fprintf(f,
        "Received: from %s by localhost (smtp_server_edu) with SMTP; %s\r\n",
        (st->helo[0] ? st->helo : "unknown"), datebuf);

    if (looks_like_headers(data_buf)) {
        fputs(data_buf, f);
    } else {
        // Cliente no incluyó headers: generamos mínimos y luego el cuerpo.
        fprintf(f, "From: %s\r\n",
                st->mail_from[0] ? st->mail_from : "<unknown@local>");
        if (st->rcpt_count > 0) {
            fprintf(f, "To: %s\r\n", st->rcpt_to[0]);
            for (int i = 1; i < st->rcpt_count; ++i) {
                fprintf(f, "Cc: %s\r\n", st->rcpt_to[i]);
            }
        }
        fprintf(f, "Date: %s\r\n", datebuf);
        fprintf(f, "MIME-Version: 1.0\r\n");
        fprintf(f, "Content-Type: text/plain; charset=\"utf-8\"\r\n");
        fprintf(f, "\r\n"); // separador headers/cuerpo
        fputs(data_buf, f);
    }

    fclose(f);
}

static void handle_client(SOCKET cs) {
    smtp_state st; reset_session(&st);
    send_line(cs, "220 localhost Simple SMTP ready");

    char line[LINE_BUFSZ];
    static char data_accum[1024 * 256];
    size_t data_len = 0;

    for (;;) {
        int r = recv_line(cs, line, sizeof(line));
        if (r <= 0) break;
        trim_crlf(line);

        if (!st.in_data) {
            if (!_strnicmp(line, "EHLO ", 5)) {
                strncpy(st.helo, line+5, sizeof(st.helo)-1);
                st.helo[sizeof(st.helo)-1] = 0;
                send_line(cs, "250-localhost greets %s", st.helo);
                send_line(cs, "250-PIPELINING");
                send_line(cs, "250-SIZE 10485760");
                send_line(cs, "250-8BITMIME");
                send_line(cs, "250 OK");
            } else if (!_strnicmp(line, "HELO ", 5)) {
                strncpy(st.helo, line+5, sizeof(st.helo)-1);
                st.helo[sizeof(st.helo)-1] = 0;
                send_line(cs, "250 Hello %s", st.helo);
            } else if (!_strnicmp(line, "MAIL FROM:", 10)) {
                const char* p = line + 10; while (*p == ' ') p++;
                strncpy(st.mail_from, p, sizeof(st.mail_from)-1);
                st.mail_from[sizeof(st.mail_from)-1] = 0;
                st.have_mail_from = 1;
                st.rcpt_count = 0;
                st.have_rcpt = 0;
                send_line(cs, "250 OK");
            } else if (!_strnicmp(line, "RCPT TO:", 8)) {
                if (!st.have_mail_from) { send_line(cs, "503 Bad sequence of commands"); continue; }
                const char* p = line + 8; while (*p == ' ') p++;
                if (st.rcpt_count < 20) {
                    strncpy(st.rcpt_to[st.rcpt_count], p, sizeof(st.rcpt_to[0])-1);
                    st.rcpt_to[st.rcpt_count][sizeof(st.rcpt_to[0])-1] = 0;
                    st.rcpt_count++;
                    st.have_rcpt = 1;
                    send_line(cs, "250 OK");
                } else {
                    send_line(cs, "452 Too many recipients");
                }
            } else if (!_stricmp(line, "DATA")) {
                if (!st.have_mail_from || !st.have_rcpt) { send_line(cs, "503 Bad sequence of commands"); continue; }
                st.in_data = 1;
                data_len = 0;
                data_accum[0] = '\0';
                send_line(cs, "354 End data with <CR><LF>.<CR><LF>");
            } else if (!_stricmp(line, "RSET")) {
                reset_session(&st);
                send_line(cs, "250 OK");
            } else if (!_stricmp(line, "NOOP")) {
                send_line(cs, "250 OK");
            } else if (!_stricmp(line, "QUIT")) {
                send_line(cs, "221 Bye");
                break;
            } else {
                send_line(cs, "500 Syntax error, command unrecognized");
            }
        } else {
            if (strcmp(line, ".") == 0) {
                st.in_data = 0;
                save_eml(&st, data_accum);
                send_line(cs, "250 OK: queued as saved");
            } else {
                rfc5321_unstuff_dot(line);
                size_t L = strlen(line);
                if (data_len + L + 2 < sizeof(data_accum)) {
                    memcpy(data_accum + data_len, line, L);
                    data_len += L;
                    data_accum[data_len++] = '\r';
                    data_accum[data_len++] = '\n';
                    data_accum[data_len] = '\0';
                }
            }
        }
    }
    closesocket(cs);
}

int main(int argc, char** argv) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    int port = (argc >= 2) ? atoi(argv[1]) : DEFAULT_PORT;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { fprintf(stderr, "socket failed\n"); WSACleanup(); return 1; }

    BOOL opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed (%d)\n", WSAGetLastError());
        closesocket(ls); WSACleanup(); return 1;
    }

    if (listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed (%d)\n", WSAGetLastError());
        closesocket(ls); WSACleanup(); return 1;
    }

    printf("SMTP server listening on port %d ...\n", port);
    ensure_dirs();

    for (;;) {
        SOCKET cs = accept(ls, NULL, NULL);
        if (cs == INVALID_SOCKET) continue;
        handle_client(cs);
    }

    closesocket(ls);
    WSACleanup();
    return 0;
}
