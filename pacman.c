#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>

#define HEIGHT 20
#define WIDTH 29

typedef struct
{ int x,y; }Position;

void init_game();
void sig_handler(int sig);
void draw_status();
void draw_pacman();
void move_pacman();
void handle_input(int ch);
void end_game(const char *message);
void run_server(int port);
void init_food_count();
void try_eat_tile(int y, int x);
void check_win();
void end_ncurses();
void ghost1_move();
void ghost2_move();
void ghost3_move();
void draw_ghost(Position ghost);
void check_cherry_time();
int resolve_collision(Position *ghost, int respawn_x, int respawn_y, int *has_target);
int prompt_quit();
int set_nonblock(int fd);
void send_state_to_clients();
void process_client_input(int player_id, int keycode);

typedef struct { int x, y; } Point;

Point queue[1000];
int front = 0;
int rear = 0;

void enqueue(Point p) { queue[rear++] = p; }

Point dequeue() { return queue[front++]; }

bool is_empty() { return front == rear; }

void reset_queue() { front = rear = 0; }


typedef enum {UP, DOWN, LEFT, RIGHT} Direction;

Position pacman;
Position ghost1, ghost2, ghost3;

Direction dir = RIGHT;
Position food;

int game_over = 0;
int score = 0;
int paused = 0;
int food_check[HEIGHT][WIDTH + 2];
int tick = 0;
int render_enabled = 1;

int cherry_time = 0;
time_t cherry_start_time;
Point cherries[4] = { {1, 2}, {27, 2}, {1, 14}, {27, 14} };
int cherry_eaten[4] = {0, 0, 0, 0};
int remaining_food = 0;

volatile sig_atomic_t quit_flag = 0;
volatile sig_atomic_t pause_flag = 0;

char map[HEIGHT][WIDTH+2];

int px = 2, py = 2;

Point ghost1_target, ghost2_target, ghost3_target;

int ghost1_has_target = 0, ghost2_has_target = 0, ghost3_has_target = 0;

// --- 서버 모드용 소켓/클라이언트 관리 ---
#define MAX_CLIENTS 2
#define DEFAULT_PORT 5000

typedef struct {
    int fd;
    int player_id;
} Client;

Client clients[MAX_CLIENTS];


Point get_next_move_bfs(Point ghost, Point target) {
    // 시작과 목표가 같으면 부모 경로가 없으니 바로 반환
    if (ghost.x == target.x && ghost.y == target.y) {
        return ghost;
    }

    bool visited[HEIGHT][WIDTH];
    Point parent[HEIGHT][WIDTH];

    for(int i=0; i<HEIGHT; i++) {
        for(int j=0; j<WIDTH; j++) {
            visited[i][j] = false;
        }
    }

    reset_queue();
    enqueue(ghost);
    visited[ghost.y][ghost.x] = true;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    bool found = false;

    while (!is_empty()) {
        Point curr = dequeue();

        if (curr.x == target.x && curr.y == target.y) {
            found = true;
            break;
        }

        for (int i = 0; i < 4; i++) {
            int nx = curr.x + dx[i];
            int ny = curr.y + dy[i];

            // 맵 범위 체크
            if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) continue;

            // 벽이 아니고(#), 아직 안 가본 곳이고, 유령집 문(-)도 아니면 이동 가능
            // (유령이 문을 통과하게 하려면 && map[ny][nx] != '-' 제거)
            if (map[ny][nx] != '#' && map[ny][nx] != '_' &&
                map[ny][nx] != '-' && map[ny][nx] != '|' &&
                !visited[ny][nx]) {
                visited[ny][nx] = true;
                parent[ny][nx] = curr;
                Point next_p = {nx, ny};
                enqueue(next_p);
            }
        }
    }

    // 3. 경로 역추적 (Backtracking)
    // 팩맨 위치에서부터 부모를 타고 거슬러 올라가서 유령 바로 앞 칸을 찾음
    if (found) {
        Point curr = target;
        // 유령의 바로 다음 위치가 나올 때까지 거슬러 올라감
        while (true) {
            Point prev = parent[curr.y][curr.x];
            // 부모가 유령의 시작 위치라면, 현재 curr가 바로 다음 이동할 칸임
            if (prev.x == ghost.x && prev.y == ghost.y) {
                return curr;
            }
            curr = prev;
        }
    }

    // 길을 못 찾았으면 제자리 유지
    return ghost;
}

// 유령 기준 5×5 안에 팩맨이 있는지 확인
int in_chase_range(Point ghost, Point pac) {
    // 가로·세로 차이가 2 이하 → 5×5 안
    if (abs(ghost.x - pac.x) <= 2 && abs(ghost.y - pac.y) <= 2) return 1;
    return 0;
}

// 벽이 아닌 랜덤 좌표 하나 뽑기
Point get_random_target() {
    while (1) {
        int x = rand() % WIDTH;
        int y = rand() % HEIGHT;

        // 지나갈 수 있는 칸만 목표로 (필요하면 조건 조정)
        if (map[y][x] != '#' && map[y][x] != '_' && map[y][x] != '-' && map[y][x] != '|') {
            Point p = {x, y};
            return p;
        }
    }
}

void init_global_state() {
    score = 0;
    game_over = 0;
    paused = 0;
    quit_flag = 0;
    pause_flag = 0;
    tick = 0;

    cherry_time = 0;
    for (int i = 0; i < 4; i++) cherry_eaten[i] = 0;
    remaining_food = 0;
}

void init_ncurses() {
    initscr();
    noecho();
    cbreak();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    timeout(50);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_RED,  COLOR_BLACK); // ghost normal
        init_pair(2, COLOR_CYAN, COLOR_BLACK); // ghost scared
    }
}

int show_menu() {
    while (1) {
        clear();
        mvprintw(5, 10, "====== PAC-MAN ======");
        mvprintw(7, 10, "1. Single Player");
        mvprintw(8, 10, "2. Two Players");
        mvprintw(10, 10, "Select (1/2): ");
        refresh();

        int ch = getch();
        if (quit_flag) {
            end_ncurses();
            printf("Terminated.\n");
            exit(0);
        }
        if (ch == '1') return 1;
        if (ch == '2') return 2;
    }
}

void end_ncurses() {
    endwin();
}

void input_map()
{
    FILE* f = fopen("map.txt", "r");
    if (f == NULL) {
        printf("파일을 열 수 없습니다.\n");
        return;
    }

    for(int i = 0; i < HEIGHT; i++)
        if(fgets(map[i], sizeof(map[i]), f) == NULL) break;

    fclose(f);

    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH + 1; j++) {
            food_check[i][j] = 0;
            if (map[i][j] == '#') food_check[i][j] = 2;
            else if (map[i][j] == ' ')
            {
                if(((4 < i && i < 11) && (9 < j && j < 19)) || (i == 9 && !(j == 8 || j == 20)))
                { food_check[i][j] = 0; }
                else { food_check[i][j] = 1; }
            }
        }
    }
    init_food_count();
}

void draw_food() {
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH + 1; j++) {
            if (food_check[i][j] != 1) continue;

            int is_cherry = 0;

            // 좌표 체리 체크
            for (int k = 0; k < 4; k++) {
                if (!cherry_eaten[k] &&
                    cherries[k].x == j &&
                    cherries[k].y == i) {
                    is_cherry = 1;
                    break;
                }
            }

            if (is_cherry) { mvprintw(i, j, "%c", '%'); }
            else { mvprintw(i, j, "."); }
        }
    }
}

void draw_map() {
    for (int i = 0; i < HEIGHT; i++) { mvprintw(i, 0, "%s", map[i]); }
    draw_food();
}

void init_food_count() {
    remaining_food = 0;
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            if (food_check[i][j] == 1) remaining_food++;
        }
    }
}

void try_eat_tile(int y, int x) {
    if (food_check[y][x] == 1) {
        score++;
        remaining_food--;
        food_check[y][x] = 0;
    }
}

void check_win() {
    if (remaining_food <= 0) {
        end_game("You Win!");
    }
}

void draw_status()
{
    mvprintw(HEIGHT, 0, "Score: %d", score);
    mvprintw(HEIGHT+1, 0, "Move: WASD | Quit: Ctrl+C | Pause: P");
    if (paused) {
        mvprintw(HEIGHT+2, 0, "Game Paused. Press P to resume.");
    }
}

void sig_handler(int sig) {
    if (sig == SIGINT) {
        quit_flag = 1;
    } else if (sig == SIGTSTP) {
        // pause_flag를 토글
        pause_flag = 1;
    }
}

int prompt_quit() {
    timeout(-1); // 블로킹 모드
    mvprintw(HEIGHT / 2, (WIDTH / 2) - 10, "Quit? (y/n): ");
    refresh();

    while (1) {
        int c = getch();
        if (c == 'y' || c == 'Y') {
            timeout(50);
            return 1;
        } else if (c == 'n' || c == 'N') {
            timeout(50);
            return 0;
        }
    }
}

int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}

void process_client_input(int player_id, int keycode) {
    // 단일 플레이어만 처리 (P1)
    if (player_id != 1) return;
    handle_input(keycode);
}

void send_state_to_clients() {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
                       "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                       tick,
                       pacman.x,  pacman.y,   // p1
                       pacman2.x, pacman2.y,  // p2
                       ghost1.x, ghost1.y,
                       ghost2.x, ghost2.y,
                       ghost3.x, ghost3.y,
                       score, remaining_food, cherry_time, paused);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            ssize_t s = send(clients[i].fd, buf, len, MSG_NOSIGNAL);
            if (s < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                close(clients[i].fd);
                clients[i].fd = -1;
            }
        }
    }
}

void run_single_player() {
    init_global_state();
    input_map();
    init_game();

    while (!game_over) {
        tick = !tick;

        if (quit_flag) {
            if (prompt_quit()) end_game("Terminated.");
            quit_flag = 0;
        }

        if (pause_flag) {
            paused = !paused;
            pause_flag = 0;
        }

        clear();
        draw_map();
        draw_pacman();

        if (paused) {
            draw_ghost(ghost1);
            draw_ghost(ghost2);
            draw_ghost(ghost3);
            draw_status();
            int ch = getch();
            handle_input(ch);
            refresh();
            usleep(80000);
            continue;
        }

        ghost1_move();
        ghost2_move();
        ghost3_move();
        draw_status();

        int ch = getch();
        handle_input(ch);
        check_cherry_time();

        if (!paused)
            move_pacman();

        // 팩맨 이동 이후 충돌 재검사
        resolve_collision(&ghost1, 12, 9, &ghost1_has_target);
        resolve_collision(&ghost2, 17, 9, &ghost2_has_target);
        resolve_collision(&ghost3, 12, 10, &ghost3_has_target);
        check_win();

        refresh();
        usleep(80000);
    }

    end_game("Game Over!");
}

void run_two_player() {
    init_global_state();
    input_map();
    init_game();

    Position pacman2 = { pacman.x + 1, pacman.y }; // P2 시작 위치

    while (!game_over) {
        tick = !tick;

        if (quit_flag) {
            if (prompt_quit()) end_game("Terminated.");
            quit_flag = 0;
        }
        if (pause_flag) {
            paused = !paused;
            pause_flag = 0;
        }

        clear();
        draw_map();
        
        draw_pacman();      // Player 1
        mvprintw(pacman2.y, pacman2.x, "Q");   // Player 2

        if (paused) {
            draw_ghost(ghost1);
            draw_ghost(ghost2);
            draw_ghost(ghost3);
            draw_status();
            int ch = getch();
            handle_input(ch);
            refresh();
            usleep(80000);
            continue;
        }

        ghost1_move();
        ghost2_move();
        ghost3_move();
        draw_status();

        int ch = getch();
        handle_input(ch);

        int next_x = pacman2.x;
        int next_y = pacman2.y;
        switch (ch) {
            case KEY_UP:    next_y--; break;
            case KEY_DOWN:  next_y++; break;
            case KEY_LEFT:  next_x--; break;
            case KEY_RIGHT: next_x++; break;
        }

        if (next_x == 0 && pacman2.y == 9) next_x = WIDTH - 1;
        else if (next_x == WIDTH && pacman2.y == 9) next_x = 1;

        if (next_x >= 0 && next_x < WIDTH && next_y >= 0 && next_y < HEIGHT) {
            if (map[next_y][next_x] == ' ' || map[next_y][next_x] == '|') {
                pacman2.x = next_x;
                pacman2.y = next_y;
            }
        }
        check_win();

        refresh();
        usleep(80000);
    }

    end_game("Game Over!");
}


void init_game() {
    pacman.x = 14;
    pacman.y = 14;
    dir = DOWN;

    ghost1.x = 12;
    ghost1.y = 9;
    ghost2.x = 17;
    ghost2.y = 9;

    ghost3.x = 12;
    ghost3.y = 10;

    score = 0;
    game_over = 0;
    paused = 0;

    cherry_time = 0;
    for (int k = 0; k < 4; k++) cherry_eaten[k] = 0;

    ghost1_has_target = 0;
    ghost2_has_target = 0;
    ghost3_has_target = 0;
}

void end_game(const char *message){
    endwin();
    printf("%s Final Score: %d\n", message, score);
    exit(0);
}

void draw_pacman(){
    if(tick) mvprintw(pacman.y, pacman.x,"C");
    else mvprintw(pacman.y, pacman.x,"c");
    if (map[pacman.y][pacman.x] != '#') {
        px = pacman.x;
        py = pacman.y;
    }
}

void move_pacman() {
    int next_x = pacman.x;
    int next_y = pacman.y;

    if(pacman.x == 1 && pacman.y == 9) { next_x = 28; }
    if(pacman.x == 28 && pacman.y == 9) { next_x = 1; }

    // 1. 현재 방향에 따라 다음에 이동할 임시 좌표 계산
    switch (dir) {
        case UP:
            next_y--;
            break;
        case DOWN:
            next_y++;
            break;
        case LEFT:
            next_x--;
            break;
        case RIGHT:
            next_x++;
            break;
    }

    // 2. 맵 범위(배열 인덱스)를 벗어나지 않는지 확인 (Safety Check)
    if (next_x < 0 || next_x >= WIDTH || next_y < 0 || next_y >= HEIGHT) {
        return;
    }

    // 3. 다음 위치가 벽('#')이 아닌 경우에만 실제 팩맨 위치 갱신
    if (map[next_y][next_x] == ' ' || map[next_y][next_x] == '|') {
        pacman.x = next_x;
        pacman.y = next_y;

        // px, py는 현재 코드에서 출력용으로는 안 쓰이지만, 동기화가 필요하다면 업데이트
        px = pacman.x;
        py = pacman.y;
    }

    try_eat_tile(py, px);

    for (int k = 0; k < 4; k++) {
        if (!cherry_eaten[k] && pacman.x == cherries[k].x && pacman.y == cherries[k].y)
        {
            cherry_eaten[k] = 1;
            cherry_time = 1;
            time(&cherry_start_time);
        }
    }

    food_check[py][px] = 0;
}


void handle_input(int ch){
    switch (ch){
    case 'w':
        dir = UP;
        break;
    case 's':
        dir = DOWN;
        break;
    case 'a':
        dir = LEFT;
        break;
    case 'd':
        dir = RIGHT;
        break;
    case 'p':
        paused = !paused;
        break;
    }
}

void draw_ghost(Position ghost)
{
    if (!render_enabled) return;
    if (cherry_time) {
        attron(COLOR_PAIR(2));
        mvprintw(ghost.y, ghost.x, "a");
        attroff(COLOR_PAIR(2));
    }

    else {
        attron(COLOR_PAIR(1));
        mvprintw(ghost.y, ghost.x, "A");
        attroff(COLOR_PAIR(1));
    }
}

int resolve_collision(Position *ghost, int respawn_x, int respawn_y, int *has_target)
{
    if (ghost->x != pacman.x || ghost->y != pacman.y) return 0;

    if (cherry_time) {
        ghost->x = respawn_x;
        ghost->y = respawn_y;
        if (has_target) *has_target = 0;
    } else {
        game_over = 1;
    }
    return 1;
}

void ghost1_move()
{
    resolve_collision(&ghost1, 12, 9, &ghost1_has_target);

    if (tick % 5 != 0) {
        draw_ghost(ghost1);

        return;
    }

    Point ghost_pos = {ghost1.x, ghost1.y};
    Point pacman_pos = {pacman.x, pacman.y};
    Point next_pos;

    if (cherry_time)
    {
        // 체리 타임: 랜덤 도망
        if (!ghost1_has_target ||
            (ghost1.x == ghost1_target.x && ghost1.y == ghost1_target.y))
        {
            ghost1_target = get_random_target();
            ghost1_has_target = 1;
        }
        next_pos = get_next_move_bfs(ghost_pos, ghost1_target);
    }
    else
    {
        // 평소 로직
        if (in_chase_range(ghost_pos, pacman_pos))
        {
            next_pos = get_next_move_bfs(ghost_pos, pacman_pos);
        }
        else
        {
            if (!ghost1_has_target ||
                (ghost1.x == ghost1_target.x && ghost1.y == ghost1_target.y))
            {
                ghost1_target = get_random_target();
                ghost1_has_target = 1;
            }
            next_pos = get_next_move_bfs(ghost_pos, ghost1_target);
        }
    }

    // 이동 적용
    ghost1.x = next_pos.x;
    ghost1.y = next_pos.y;

    //resolve_collision(&ghost1, 12, 9, &ghost1_has_target);
    // 유령 그리기 (색/문자는 draw_ghost가 처리)
    draw_ghost(ghost1);
}

void ghost2_move()
{
    resolve_collision(&ghost2, 17, 9, &ghost2_has_target);

    if (tick % 5 != 1) {
        draw_ghost(ghost2);
        return;
    }

    Point ghost_pos = {ghost2.x, ghost2.y};
    Point pacman_pos = {pacman.x, pacman.y};
    Point next_pos;

    if (cherry_time)
    {
        // 체리 타임: 랜덤 도망
        if (!ghost2_has_target ||
            (ghost2.x == ghost2_target.x && ghost2.y == ghost2_target.y))
        {
            ghost2_target = get_random_target();
            ghost2_has_target = 1;
        }
        next_pos = get_next_move_bfs(ghost_pos, ghost2_target);
    }
    else
    {
        // 평소 로직
        if (in_chase_range(ghost_pos, pacman_pos))
        {
            next_pos = get_next_move_bfs(ghost_pos, pacman_pos);
        }
        else
        {
            if (!ghost2_has_target ||
                (ghost2.x == ghost2_target.x && ghost2.y == ghost2_target.y))
            {
                ghost2_target = get_random_target();
                ghost2_has_target = 1;
            }
            next_pos = get_next_move_bfs(ghost_pos, ghost2_target);
        }
    }

    ghost2.x = next_pos.x;
    ghost2.y = next_pos.y;

    //resolve_collision(&ghost2, 17, 9, &ghost2_has_target);
    draw_ghost(ghost2);
}

void ghost3_move()
{
    resolve_collision(&ghost3, 12, 10, &ghost3_has_target);

    if (tick != 0) {
        draw_ghost(ghost3);
        return;
    }

    Point ghost_pos = {ghost3.x, ghost3.y};
    Point pacman_pos = {pacman.x, pacman.y};
    Point next_pos;

    if (cherry_time)
    {
        if (!ghost3_has_target ||
            (ghost3.x == ghost3_target.x && ghost3.y == ghost3_target.y))
        {
            ghost3_target = get_random_target();
            ghost3_has_target = 1;
        }
        next_pos = get_next_move_bfs(ghost_pos, ghost3_target);
    }
    else
    {
        if (in_chase_range(ghost_pos, pacman_pos))
        {
            next_pos = get_next_move_bfs(ghost_pos, pacman_pos);
        }
        else
        {
            if (!ghost3_has_target ||
                (ghost3.x == ghost3_target.x && ghost3.y == ghost3_target.y))
            {
                ghost3_target = get_random_target();
                ghost3_has_target = 1;
            }
            next_pos = get_next_move_bfs(ghost_pos, ghost3_target);
        }
    }

    ghost3.x = next_pos.x;
    ghost3.y = next_pos.y;

    //resolve_collision(&ghost3, 12, 10, &ghost3_has_target);
    draw_ghost(ghost3);
}

void check_cherry_time()
{
    if (cherry_time) {
        time_t now;
        time(&now);
        if (difftime(now, cherry_start_time) >= 10.0) {
            cherry_time = 0;    // 10초 지나면 체리 효과 꺼짐
        }
    }

}

void run_server(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblock(listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(1);
    }
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        exit(1);
    }

    for (int i = 0; i < MAX_CLIENTS; i++) { clients[i].fd = -1; clients[i].player_id = i + 1; }

    init_global_state();
    input_map();
    init_game();
    render_enabled = 0; // headless

    while (!game_over) {
        tick = !tick;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            }
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 80000; // 80ms 틱

        int rv = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rv < 0 && errno != EINTR) {
            perror("select");
        }

        // 새 클라이언트 수락
        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr*)&caddr, &clen);
            if (cfd >= 0) {
                set_nonblock(cfd);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) {
                        clients[i].fd = cfd;
                        break;
                    }
                }
            }
        }

        // 입력 처리
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0 && FD_ISSET(clients[i].fd, &rfds)) {
                char buf[32];
                ssize_t n = recv(clients[i].fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    close(clients[i].fd);
                    clients[i].fd = -1;
                } else {
                    for (ssize_t k = 0; k < n; k++) {
                        process_client_input(clients[i].player_id, buf[k]);
                    }
                }
            }
        }

        if (pause_flag) { paused = !paused; pause_flag = 0; }
        if (quit_flag) { game_over = 1; }
        if (paused) { send_state_to_clients(); continue; }

        ghost1_move();
        ghost2_move();
        ghost3_move();
        check_cherry_time();
        move_pacman();

        resolve_collision(&ghost1, 12, 9, &ghost1_has_target);
        resolve_collision(&ghost2, 17, 9, &ghost2_has_target);
        resolve_collision(&ghost3, 12, 10, &ghost3_has_target);
        check_win();

        send_state_to_clients();
    }

    // 종료 통지
    const char *end_msg = "END GameOver\n";
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) send(clients[i].fd, end_msg, strlen(end_msg), 0);
    }
    close(listen_fd);
}


int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--server") == 0) {
        int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;
        srand(time(NULL));
        signal(SIGINT, sig_handler);
        signal(SIGTSTP, sig_handler);
        run_server(port);
        return 0;
    }
    srand(time(NULL));
    signal(SIGINT, sig_handler); // 종료 시그널
    signal(SIGTSTP, sig_handler); // 일시정지 시그널
    init_ncurses();

    int mode = show_menu();

    if (mode == 1)
        run_single_player();
    else
        run_two_player();

    end_ncurses();
    return 0;
}
