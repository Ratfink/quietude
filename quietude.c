// $ gcc -g -o quietude quietude.c `pkg-config --cflags --libs libbone`
// # ./quietude
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <libbone/libbone.h>
#include <libbone/ssd1306.h>

#define DEBOUNCE_DELAY 150000
#define SCROLLBACK 256
#define CONSOLE_WIDTH 20
#define CONSOLE_LINES 8
#define BAUDRATE B115200
#define REPRAPDEVICE "/dev/ttyACM0"
#define _POSIX_SOURCE 1
#define WRITE_TO_STDOUT


bone_ssd1306_t *display;
struct pollfd pfd[6];
char *ss[SCROLLBACK];
bool eof_seen = false;
int nlines = 0;
bool cts = false;


ssize_t read_line(int filedes, char *buf, size_t nbyte)
{
    ssize_t total = 0, numread;
    char *bufmax = buf + nbyte;

    for (; buf < bufmax; buf++) {
        numread = read(filedes, buf, 1);
        if (numread < 0)
            return -1;
        total += numread;
        if (numread == 0 || *buf == '\n')
            break;
    }

    return total;
}


int write_console(char *str)
{
    int i;
    char *st;
    bool strend = false, charisspace;

    do {
        for (i = 0; i < CONSOLE_WIDTH; i++) {
            ss[0][i] = str[i];
            if (str[i] == '\0') {
                strend = true;
                break;
            }
        }
        str += i;
        ss[0][i+1] = '\0';
#ifdef WRITE_TO_STDOUT
        printf("%s", ss[0]);
#endif /* WRITE_TO_STDOUT */
        // TODO: make this strip trailing whitespace instead of (effectively)
        // checking only the last character
        for (i = 0; i < CONSOLE_WIDTH; i++) {
            if (ss[0][i] == '\0')
                break;
            charisspace = isspace(ss[0][i]);
            if (!charisspace)
                break;
        }
        if (charisspace)
            break;
        if (nlines < SCROLLBACK)
            nlines++;
        st = ss[0];
        for (i = 1; i < SCROLLBACK; i++) {
            ss[i-1] = ss[i];
        }
        ss[SCROLLBACK-1] = st;
    } while (!strend);
}


int get_lead(int fd)
{
    int value;
    lseek(fd, 0, 0);

    char buffer[2];
    int size = read(fd, buffer, sizeof(buffer));
    if (size != -1) {
        buffer[size] = '\0';
        value = atoi(buffer);
    } else {
        value = -1;
    }

    return value;
}


int handle_printer(void)
{
    int numread;
    char buf[256];
    int i;
    char *write;

    do {
        numread = read_line(pfd[0].fd, buf, 255);
        if (numread <= 0)
            eof_seen = true;
        buf[numread] = '\0';
        write = buf;
        // "start" isn't a very useful message to the user
        if (strncmp(buf, "start", 5) == 0) {
            write = NULL;
            cts = true;
        }
        // Strip out debug message prefixes, as our display is quite small
        if (strncmp(buf, "echo:", 5) == 0) {
            write = buf + 5;
        }
        if (strncmp(buf, "//", 2) == 0) {
            write = buf + 2;
        }
        // Don't fill the console with "ok"
        if (strncmp(buf, "ok", 2) == 0) {
            write = NULL;
            cts = true;
        }

        // Write to the console if the message wasn't set hidden above
        if (write != NULL) {
            while (isspace(write[0]) && write[0] != '\0')
                write++;
            write_console(write);
        }
        // Poll the printer without blocking to see if we've read everything
        poll(pfd, 1, 0);
        // If we haven't read everything available and more data will ever be
        // available, keep iterating
    } while (pfd[0].revents != 0 && !eof_seen);
}


void keyboard(void)
{
    int i;
    static const char keys[6][7] = {
        {'A', 'B', 'C', 'D', 'E', 'F', '^'},
        {'G', 'H', 'I', 'J', 'K', 'L', 'v'},
        {'M', 'N', 'O', 'P', 'Q', 'R', '<'},
        {'S', 'T', 'U', 'V', 'W', 'X', '.'},
        {'Y', 'Z', '0', '1', '2', '3', '_'},
        {'4', '5', '6', '7', '8', '9', '@'}
    };
    char cmd[256];
    int nbyte = 0;
    int x = 0, y = 1;
    int dx, dy;
    bool enter = false;

    bone_ssd1306_clear(display, 0);

    while (!enter) {
        for (dy = 0; dy < 6; dy++) {
            for (dx = 0; dx < 7; dx++) {
                bone_ssd1306_char(display, 1+12*dx, 56-8*dy, 1, keys[dy][dx]);
            }
        }
        bone_ssd1306_rect(display, 12*x, 56-8*y, 12*x+6, 64-8*y, 1);
        bone_ssd1306_char(display, 1+12*x, 56-8*y, 0, keys[y][x]);
        for (i = 0; i < nbyte; i++) {
            bone_ssd1306_char(display, 6*i, 0, 1, cmd[i]);
        }
        bone_ssd1306_draw(display);

        poll(pfd, 6, -1);
        bone_ssd1306_clear(display, 0);
        if (pfd[0].revents != 0) {
            handle_printer();
        }
        if (pfd[1].revents & POLLPRI) {
            get_lead(pfd[1].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[1].fd);
            if (y > 0)
                y--;
            else
                y = 5;
        }
        if (pfd[2].revents & POLLPRI) {
            get_lead(pfd[2].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[2].fd);
            if (y < 5)
                y++;
            else
                y = 0;
        }
        if (pfd[3].revents & POLLPRI) {
            get_lead(pfd[3].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[5].fd);
            if (x < 6)
                x++;
            else
                x = 0;
        }
        if (pfd[4].revents & POLLPRI) {
            get_lead(pfd[4].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[5].fd);
            if (x > 0)
                x--;
            else
                x = 6;
        }
        if (pfd[5].revents & POLLPRI) {
            get_lead(pfd[5].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[5].fd);
            if (keys[y][x] == '_') {
                cmd[nbyte] = ' ';
            } else if (keys[y][x] == '<') {
                nbyte -= 2;
            } else if (keys[y][x] == '^') {
                nbyte--;
            } else if (keys[y][x] == 'v') {
                nbyte--;
            } else if (keys[y][x] == '@') {
                cmd[nbyte] = '\n';
                write(pfd[0].fd, cmd, nbyte+1);
                cmd[nbyte] = '\0';
                write_console(cmd);
                nbyte = 0;
                enter = true;
            } else {
                cmd[nbyte] = keys[y][x];
            }
            // Jump to '0' if a letter was just typed
            if (cmd[nbyte] >= 'A' && cmd[nbyte] <= 'Z' && keys[y][x] != '<') {
                x = 2;
                y = 4;
            }
            nbyte++;
        }
    }
}


void console(void)
{
    int i, value;
    static int pos = SCROLLBACK - 8;

    for (i = 1; i < 6; i++)
        get_lead(pfd[i].fd);

    while (!eof_seen) {
        for (i = 0; i < CONSOLE_LINES; i++) {
            bone_ssd1306_str(display, 0, 56-8*i, 1, ss[i+pos]);
        }
        bone_ssd1306_line(display, 121, 0, 121, 63, 1);
        bone_ssd1306_str(display, 123, 56, 1, "C\nO\nN\nS\nO\nL\nE");
        bone_ssd1306_line(display, 120,
                8*CONSOLE_LINES*(1-(pos+CONSOLE_LINES-SCROLLBACK+nlines)/(float)nlines),
                120, 8*CONSOLE_LINES*(1-(pos-SCROLLBACK+nlines)/(float)nlines), 1);
        bone_ssd1306_draw(display);

        poll(pfd, 6, -1);
        bone_ssd1306_clear(display, 0);
        if (pfd[0].revents != 0) {
            handle_printer();
        }
        if (pfd[1].revents & POLLPRI) {
            get_lead(pfd[1].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[1].fd);
            if (pos > SCROLLBACK - nlines)
                pos -= 1;
        }
        if (pfd[2].revents & POLLPRI) {
            get_lead(pfd[2].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[2].fd);
            if (pos < SCROLLBACK - CONSOLE_LINES)
                pos += 1;
        }
        if (pfd[3].revents & POLLPRI) {
            get_lead(pfd[3].fd);
        }
        if (pfd[4].revents & POLLPRI) {
            get_lead(pfd[4].fd);
        }
        if (pfd[5].revents & POLLPRI) {
            get_lead(pfd[5].fd);
            usleep(DEBOUNCE_DELAY);
            get_lead(pfd[5].fd);
            keyboard();
        }
    }
}


int main(int argc, char *argv[])
{
    int i;
    struct termios oldtio, newtio;

    display = bone_ssd1306_init(P8+3, I2C, 1, 0x3c, 128, 64);

    pfd[0].fd = open(REPRAPDEVICE, O_RDWR | O_NOCTTY); 
    if (pfd[0].fd < 0) {
        perror(REPRAPDEVICE);
        return 1;
    }

    tcgetattr(pfd[0].fd, &oldtio);
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR | ICRNL;
    newtio.c_oflag = 0;
    newtio.c_lflag = ICANON;

    newtio.c_cc[VINTR] = 0;
    newtio.c_cc[VQUIT] = 0;
    newtio.c_cc[VERASE] = 0;
    newtio.c_cc[VKILL] = 0;
    newtio.c_cc[VEOF] = 4;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 1; 
    newtio.c_cc[VSWTC] = 0; 
    newtio.c_cc[VSTART] = 0; 
    newtio.c_cc[VSTOP] = 0;
    newtio.c_cc[VSUSP] = 0;
    newtio.c_cc[VEOL] = 0;
    newtio.c_cc[VREPRINT] = 0;
    newtio.c_cc[VDISCARD] = 0;
    newtio.c_cc[VWERASE] = 0;
    newtio.c_cc[VLNEXT] = 0;
    newtio.c_cc[VEOL2] = 0;

    tcflush(pfd[0].fd, TCIFLUSH);
    tcsetattr(pfd[0].fd, TCSANOW, &newtio);

    bone_gpio_export(P9+14);
    bone_gpio_export(P9+15);
    bone_gpio_export(P9+16);
    bone_gpio_export(P9+23);
    bone_gpio_export(P9+25);
    bone_gpio_set_dir(P9+14, 0);
    bone_gpio_set_dir(P9+15, 0);
    bone_gpio_set_dir(P9+16, 0);
    bone_gpio_set_dir(P9+23, 0);
    bone_gpio_set_dir(P9+25, 0);
    bone_gpio_set_edge(P9+14, (enum bone_gpio_edge) RISING);
    bone_gpio_set_edge(P9+15, (enum bone_gpio_edge) RISING);
    bone_gpio_set_edge(P9+16, (enum bone_gpio_edge) RISING);
    bone_gpio_set_edge(P9+23, (enum bone_gpio_edge) RISING);
    bone_gpio_set_edge(P9+25, (enum bone_gpio_edge) RISING);

    pfd[1].fd = bone_gpio_open_value(P9+14);
    pfd[2].fd = bone_gpio_open_value(P9+15);
    pfd[3].fd = bone_gpio_open_value(P9+16);
    pfd[4].fd = bone_gpio_open_value(P9+23);
    pfd[5].fd = bone_gpio_open_value(P9+25);
    for (i = 1; i <= 5; i++) {
        if (pfd[i].fd < 0) {
            bone_ssd1306_free(display);
            return -1;
        }
    }
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;
    for (i = 1; i <= 5; i++) {
        pfd[i].events = POLLPRI | POLLERR;
        pfd[i].revents = 0;
    }

    for (i = 0; i < SCROLLBACK; i++) {
        ss[i] = calloc(CONSOLE_WIDTH + 1, 1);
        if (ss[i] == NULL)
            return 1;
    }
    bone_ssd1306_setup(display);

    // Run the program
    console();

    bone_ssd1306_free(display);
    for (i = 0; i <= 5; i++)
        close(pfd[i].fd);
    bone_gpio_unexport(P9+14);
    bone_gpio_unexport(P9+15);
    bone_gpio_unexport(P9+16);
    bone_gpio_unexport(P9+23);
    bone_gpio_unexport(P9+25);
    for (i = 0; i < SCROLLBACK; i++)
        free(ss[i]);

    return 0;
}
