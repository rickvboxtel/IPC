#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


#define SOCK_PATH "../echo_socket"

int main(void)
{
    int                 s, s2, t, len;
    struct sockaddr_un  local, remote;
    char                str[100];
    int                 stat = 0;
    int last_stat = 0;

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCK_PATH);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);

    if (bind(s, (struct sockaddr *)&local, len) == -1) {
        perror("bind");
        close(s);
        exit(1);
    }

    if (listen(s, 5) == -1) {
        perror("listen");
        close(s);
        exit(1);
    }

    for(;;) {
        int n;
        printf("Waiting for a connection...\n");
        t = sizeof(remote);
        if ((s2 = accept(s, (struct sockaddr *)&remote, (socklen_t*)&t)) == -1) {
            perror("accept");
            close(s);
            exit(1);
        }

        printf("Connected.\n");

        do {
            uint8_t  buffer[1400];
            uint32_t frame_len;
            int now = time(NULL);

            n = recv(s2, &frame_len, sizeof(frame_len), 0);
            frame_len = be32toh(frame_len);

            if (n < sizeof(uint32_t)) {
                break;
            }

            n = recv(s2, buffer, frame_len, 0);

            if (frame_len > 0) {
                if (n < frame_len) {
                    close(s);
                    perror("recv");
                    break;
                }
            }


            stat += frame_len + sizeof(uint32_t);

            if (now - last_stat > 1) {
                last_stat = now;
                printf("received %f MB.\n", ((float)stat / 1024 / 1024));
                stat = 0;
            }
        } while (1);

        close(s2);
    }

    return 0;
}
