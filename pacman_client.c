#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <ncurses.h>
#include "common.h" //공통작업 헤더

int sock;
GameState state;
pthread_mutex_t lock;

void init_ncurses()
{
    initscr();
    noecho();
    cbreak();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    timeout(0);

    if (has_colors())
    {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_RED, COLOR_BLACK);
        init_pair(2, COLOR_CYAN, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_GREEN, COLOR_BLACK);
    }
}

// 서버한테 데이터를 계속 받아오는 쓰레드 함수
void *recv_msg(void *arg)
{
    GameState temp_state;
    while (1)
    {
        // 서버한테 구조체 크기만큼 읽음
        int str_len = read(sock, &temp_state, sizeof(GameState));
        if (str_len <= 0)
        {
            // 연결 끊김
            endwin();
            printf("Server disconnected.\n");
            exit(0);
        }

        // 받은 데이터를 전역 변수에 업데이트 (그릴때 데이터가 바뀌면 안되니까 잠금)
        pthread_mutex_lock(&lock);
        state = temp_state;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

void draw_game() {
    pthread_mutex_lock(&lock); // 읽을때 데이터 변경 방지

    // 1. 맵 그리기
    for (int i = 0; i < HEIGHT; i++) { mvprintw(i, 0, "%s", state.map[i]); }

    // 2. 플레이어 1 그리기
    attron(COLOR_PAIR(3));
    if (state.cherry_time) mvprintw(state.p1_y, state.p1_x, "C");
    else mvprintw(state.p1_y, state.p1_x, "c");
    attroff(COLOR_PAIR(3));

    // 3. 플레이어 2 그리기
    attron(COLOR_PAIR(4));
    mvprintw(state.p2_y, state.p2_x, "Q");
    attroff(COLOR_PAIR(4));

    // 4. 유령 그리기
    int color = state.cherry_time ? 2 : 1;
    char ghost_char = state.cherry_time ? 'a' : 'A';

    attron(COLOR_PAIR(color));
    mvprintw(state.g1_y, state.g1_x, "%c", ghost_char);
    mvprintw(state.g2_y, state.g2_x, "%c", ghost_char);
    mvprintw(state.g3_y, state.g3_x, "%c", ghost_char);
    attroff(COLOR_PAIR(color));

    // 5. 상태창
    mvprintw(HEIGHT, 0, "Score: %d", state.score);
    if (state.game_over) { mvprintw(HEIGHT + 2, 5, "GAME OVER! Final Score: %d", state.score); }

    pthread_mutex_unlock(&lock);
    refresh();
}

void error_handling(char *message)
{
    endwin();
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    pthread_t rcv_thread;

    if (argc != 2)
    {
        printf("Usage : %s <PlayerID 1 or 2>\n", argv[0]);
        exit(1);
    }

    int my_id = atoi(argv[1]); //아이디 지정

    //소켓 연결 설정
    sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(IP);
    serv_addr.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    //초기화
    pthread_mutex_init(&lock, NULL);
    init_ncurses();

    //수신전용 쓰레드 시작
    pthread_create(&rcv_thread, NULL, recv_msg, NULL);

    while (1) {
        int ch = getch();
        if (ch != ERR)
        {
            InputPacket packet;
            packet.key = ch;
            packet.player_id = my_id;

            // 서버로 키 전송
            write(sock, &packet, sizeof(InputPacket));

            if (ch == 'q') break;
        }

        draw_game();
        usleep(30000);
    }

    pthread_join(rcv_thread, NULL);
    close(sock);
    endwin();
    return 0;
}
