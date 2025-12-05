#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <ncurses.h>
#include <errno.h>

#include "common.h"   // IP, PORT, HEIGHT, WIDTH 정의되어 있다고 가정

int sock;

typedef struct {
    int x, y;
} Point;

typedef struct {
    int tick;
    int pac_x, pac_y;
    int g1_x, g1_y;
    int g2_x, g2_y;
    int g3_x, g3_y;
    int score;
    int remaining_food;
    int cherry_time;
    int paused;
    int game_over;
} ClientState;

ClientState state;
pthread_mutex_t lock;
volatile int state_ready = 0;
int curses_initialized = 0;

char map_data[HEIGHT][WIDTH + 2];
int food_grid[HEIGHT][WIDTH + 2];
Point cherries[4] = { {1, 2}, {27, 2}, {1, 14}, {27, 14} };
int cherry_eaten[4] = {0, 0, 0, 0};

// 맵은 서버가 안 보내주므로 로컬 map.txt에서 읽음
void load_map()
{
    FILE *f = fopen("map.txt", "r");
    if (!f) {
        perror("map.txt open");
        exit(1);
    }

    for (int i = 0; i < HEIGHT; i++) {
        if (!fgets(map_data[i], sizeof(map_data[i]), f)) {
            map_data[i][0] = '\0';
        } else {
            size_t len = strlen(map_data[i]);
            if (len > 0 && map_data[i][len - 1] == '\n') {
                map_data[i][len - 1] = '\0';
            }
        }
    }

    fclose(f);

    // 서버와 동일한 규칙으로 음식 배치
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH + 1; j++) {
            food_grid[i][j] = 0;
            if (map_data[i][j] == '#') {
                food_grid[i][j] = 2;  // 벽
            } else if (map_data[i][j] == ' ') {
                if (((4 < i && i < 11) && (9 < j && j < 19)) || (i == 9 && !(j == 8 || j == 20)))
                    food_grid[i][j] = 0; // 고스트 하우스 내부
                else
                    food_grid[i][j] = 1; // 먹이
            }
        }
    }
}

void init_ncurses()
{
    initscr();
    noecho();
    cbreak();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    timeout(0);  // non-blocking 입력

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_RED,   COLOR_BLACK);   // 평소 유령
        init_pair(2, COLOR_CYAN,  COLOR_BLACK);   // 체리타임 유령
        init_pair(3, COLOR_YELLOW,COLOR_BLACK);   // 플레이어
        init_pair(4, COLOR_GREEN, COLOR_BLACK);   // 필요시 추가 용도
    }

    curses_initialized = 1;
}

// 한 줄을 파싱해서 state 갱신
void handle_state_line(const char *line)
{
    char tag[16];
    int tick;
    int pac_x, pac_y;
    int g1x, g1y;
    int g2x, g2y;
    int g3x, g3y;
    int score;
    int remaining_food;
    int cherry_time;
    int paused;

    // 포맷:
    // STATE tick pac_x pac_y g1x g1y g2x g2y g3x g3y score remaining_food cherry_time paused
    int n = sscanf(line,
                   "%15s %d %d %d %d %d %d %d %d %d %d %d %d %d",
                   tag,
                   &tick,
                   &pac_x, &pac_y,
                   &g1x, &g1y,
                   &g2x, &g2y,
                   &g3x, &g3y,
                   &score,
                   &remaining_food,
                   &cherry_time,
                   &paused);

    if (n == 14 && strcmp(tag, "STATE") == 0) {
        pthread_mutex_lock(&lock);
        state.tick           = tick;
        state.pac_x          = pac_x;
        state.pac_y          = pac_y;
        state.g1_x           = g1x;
        state.g1_y           = g1y;
        state.g2_x           = g2x;
        state.g2_y           = g2y;
        state.g3_x           = g3x;
        state.g3_y           = g3y;
        state.score          = score;
        state.remaining_food = remaining_food;
        state.cherry_time    = cherry_time;
        state.paused         = paused;
        // game_over는 서버가 END로 알려주므로 여기선 건들지 않음
        if (pac_y >= 0 && pac_y < HEIGHT && pac_x >= 0 && pac_x < WIDTH + 1)
            food_grid[pac_y][pac_x] = 0; // 클라이언트에서도 먹은 자리는 지움
        for (int i = 0; i < 4; i++) {
            if (pac_x == cherries[i].x && pac_y == cherries[i].y) {
                cherry_eaten[i] = 1;
            }
        }
        state_ready = 1;
        pthread_mutex_unlock(&lock);
    }
}

// 서버에서 오는 라인을 계속 읽는 쓰레드
void *recv_thread_func(void *arg)
{
    char buf[512];
    int idx = 0;

    while (1) {
        char c;
        ssize_t r = read(sock, &c, 1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            endwin();
            perror("read");
            exit(1);
        }
        if (r == 0) {
            // 서버가 연결 종료
            endwin();
            printf("Server disconnected.\n");
            exit(0);
        }

        if (c == '\r') {
            // CRLF이면 CR은 버림
            continue;
        }

        if (c == '\n') {
            buf[idx] = '\0';
            idx = 0;

            if (strncmp(buf, "STATE", 5) == 0) {
                handle_state_line(buf);
            } else if (strncmp(buf, "END", 3) == 0) {
                // 예: "END GameOver"
                endwin();
                if (buf[3] == ' ' || buf[3] == '\t')
                    printf("%s\n", buf + 4);
                else
                    printf("%s\n", buf);
                exit(0);
            } else {
                // 그 외 메시지는 필요하면 디버깅용으로 쓸 수 있음
                // printf("MSG: %s\n", buf);
            }

        } else {
            if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = c;
            }
            // 줄이 너무 길면 그냥 잘라버림
        }
    }

    return NULL;
}

void draw_game()
{
    if (!state_ready) {
        mvprintw(0, 0, "Waiting for server state...");
        refresh();
        return;
    }

    pthread_mutex_lock(&lock);

    // 맵 그리기
    for (int i = 0; i < HEIGHT; i++) {
        mvprintw(i, 0, "%s", map_data[i]);
    }

    // 먹이 그리기 (벽이나 문자 위에는 덮어쓰지 않음)
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (food_grid[y][x] == 1 && map_data[y][x] == ' ') {
                mvprintw(y, x, ".");
            }
        }
    }

    // 체리 표시 (안 먹은 것만)
    for (int i = 0; i < 4; i++) {
        if (!cherry_eaten[i]) {
            mvprintw(cherries[i].y, cherries[i].x, "%%");
        }
    }

    // 플레이어 그리기
    attron(COLOR_PAIR(3));
    if (state.cherry_time)
        mvprintw(state.pac_y, state.pac_x, "C");
    else
        mvprintw(state.pac_y, state.pac_x, "c");
    attroff(COLOR_PAIR(3));

    // 유령 색
    int ghost_color = state.cherry_time ? 2 : 1;
    char ghost_char = state.cherry_time ? 'a' : 'A';

    attron(COLOR_PAIR(ghost_color));
    mvprintw(state.g1_y, state.g1_x, "%c", ghost_char);
    mvprintw(state.g2_y, state.g2_x, "%c", ghost_char);
    mvprintw(state.g3_y, state.g3_x, "%c", ghost_char);
    attroff(COLOR_PAIR(ghost_color));

    // 상태창
    mvprintw(HEIGHT, 0, "Score: %d  Remain: %d", state.score, state.remaining_food);
    if (state.paused)
        mvprintw(HEIGHT + 1, 0, "[PAUSED]");
    else
        mvprintw(HEIGHT + 1, 0, "        ");

    pthread_mutex_unlock(&lock);

    refresh();
}

void error_handling(const char *msg)
{
    if (curses_initialized)
        endwin();
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    pthread_t rcv_thread;

    if (argc != 2) {
        printf("Usage : %s <PlayerID 1 or 2>\n", argv[0]);
        return 1;
    }

    int my_id = atoi(argv[1]);  // 지금 프로토콜상 서버에 직접 보내진 않지만, 필요하면 나중에 씀
    (void)my_id;                // 사용 안 한다고 경고 안 뜨게

    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&lock, NULL);

    load_map();
    init_ncurses();

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(IP);
    serv_addr.sin_port        = htons(PORT);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    // 수신 쓰레드 시작
    pthread_create(&rcv_thread, NULL, recv_thread_func, NULL);

    while (1) {
        int ch = getch();
        if (ch != ERR) {
            int send_key = -1;

            switch (ch) {
                case KEY_UP:    send_key = 'w'; break;
                case KEY_DOWN:  send_key = 's'; break;
                case KEY_LEFT:  send_key = 'a'; break;
                case KEY_RIGHT: send_key = 'd'; break;
                default:
                    if (ch >= 0 && ch <= 0xFF)
                        send_key = ch;
                    break;
            }

            // 키를 1바이트 그대로 보냄
            // 서버는 w a s d p, 그리고 0x03만 사용
            if (send_key != -1) {
                unsigned char key = (unsigned char)send_key;
                if (write(sock, &key, 1) <= 0) {
                    endwin();
                    perror("write");
                    exit(1);
                }
            }

            // 로컬에서 q 누르면 그냥 클라이언트 종료
            if (ch == 'q')
                break;
        }

        draw_game();
        usleep(30000);
    }

    endwin();
    close(sock);
    pthread_cancel(rcv_thread);
    pthread_join(rcv_thread, NULL);
    return 0;
}
