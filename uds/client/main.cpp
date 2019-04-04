#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

int epollfd;
#define SERVERPORT 8080
#define MAXCONN    5
#define MAXEVENTS  100
#define MAXLEN     1400
#define SOCK_PATH  "../echo_socket"

typedef struct event_t {
    int      fd;
    uint32_t event;
    char     data[MAXLEN];
    int      length;
    int      offset;
} event_t;

typedef struct proto_t {
    uint32_t        len;
    uint8_t        *data;
} proto_t;

static void event_set(int epollfd, int op, int fd, uint32_t events, void* data)
{
    struct epoll_event server_ev;

    server_ev.events   = events;
    server_ev.data.ptr = data;

    if(-1 == epoll_ctl(epollfd, op, fd, &server_ev)) {
        printf("Failed to add an event for socket%d Error:%s\n",
               fd, strerror(errno));
        exit(1);
    }

}

static void event_handle(void* ptr)
{
    event_t  *ev = (event_t*)ptr;

    if(EPOLLIN == ev->event) {
        return;
    } else
    if(EPOLLOUT == ev->event) {
        int      ret;
        proto_t *frame = (proto_t *)ev->data;

        if (ev->length == 0) {
            /* init send */
            ev->length = (rand() % (sizeof(ev->data)
                                   - sizeof(uint32_t))
                                   + sizeof(uint32_t));

            frame->len = htobe32(ev->length - sizeof(uint32_t));
        }


        ret = write(ev->fd, (ev->data) + (ev->offset), ev->length);
        if( (ret < 0 && EINTR == errno) || ret <= ev->length) {
            /*
             * We either got EINTR or write only sent partial data.
             * Add an write event. We still need to write data.
             */

            if(ret > 0) {
               /*
               * The previous write wrote only partial data to the socket.
               */
                ev->length = ev->length - ret;
                ev->offset = ev->offset + ret;
            }

            if (ev->length == 0) {
                /* write complete */
                ev->offset = 0;
                ev->length = 0;
            }
        } else
        if(ret < 0) {
            /*
             * Some other error occured.
             */
            printf("ERROR: ret < 0");
            close(ev->fd);
            free(ev);
            return;
        }
        if (ret == 0) {
            printf("------------\n");
        }
        event_set(epollfd, EPOLL_CTL_ADD, ev->fd, EPOLLOUT, ev);
    }
}


static void socket_set_non_blocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, NULL);

    if (flags < 0) {
        printf("fcntl F_GETFL failed.%s", strerror(errno));
        exit(1);
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) < 0) {
        printf("fcntl F_SETFL failed.%s", strerror(errno));
        exit(1);
    }
}


int main(int argc, char** argv)
{
    int                 clientfd;
    int                 len    = 0;
    struct sockaddr_un  remote;
    struct epoll_event *events = NULL;
    event_t             ev;

   /*
    * Create server socket. Specify the nonblocking socket option.
    *
    */
    clientfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if(clientfd < 0)
    {
        printf("Failed to create socket.%s\n", strerror(errno));
        exit(1);
    }

    bzero(&remote, sizeof(remote));

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);

   /*
    * connect to the server
    *
    */
    if (connect(clientfd, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Connected.\n");

   /*
    * Create epoll context.
    */
    epollfd = epoll_create(1);

    if(epollfd < 0)
    {
        printf("Failed to create epoll context.%s\n", strerror(errno));
        exit(1);
    }

    /*
     * Main loop that listens for event.
     */
    events = (epoll_event*)calloc(MAXEVENTS, sizeof(struct epoll_event));
    bzero(&ev, sizeof(ev));
    ev.fd = clientfd;
    event_set(epollfd, EPOLL_CTL_ADD, clientfd, EPOLLOUT, &ev);

   while(1) {
        int n = epoll_wait(epollfd, events, MAXEVENTS, -1);

        if(n < 0) {
            printf("Failed to wait.%s\n", strerror(errno));
            exit(1);
        }

        for(int i = 0; i < n; i++) {
            event_t *event = (event_t *)events[i].data.ptr;

            if(events[i].events & EPOLLHUP || events[i].events & EPOLLERR) {
                printf("\nClosing connection socket\n");
                close(event->fd);
            } else
            if(EPOLLIN == events[i].events) {
                event->event = EPOLLOUT;
                event_set(epollfd, EPOLL_CTL_DEL, event->fd, 0, 0);
                printf("ERROR, Cannot receive data\n");
                //event_handle(ev);
            } else
            if(EPOLLOUT == events[i].events) {
                event->event = EPOLLOUT;
                /*
                 * Delete the write event.
                 */
                event_set(epollfd, EPOLL_CTL_DEL, event->fd, 0, 0);
                event_handle(event);
            }
        }
   }

    free(events);
    exit(0);
}
