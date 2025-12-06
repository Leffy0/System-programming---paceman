#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define HEIGHT 20
#define WIDTH 29
#define WIN_FOOD_TARGET 206
#define FLEE_MIN_DIST2 64  // 체리 타임 시 이 거리^2 이상 떨어진 위치를 우선 선택

#define MAX_CLIENTS 2
#define DEFAULT_PORT 5001

typedef struct { int x, y; } Point; //가야할곳
typedef struct { int x, y; } Position; //위치
typedef enum { UP, DOWN, LEFT, RIGHT } Direction; //방향
typedef enum { MODE_SINGLE = 1, MODE_COUPLE = 2 } Mode; // 모드

Position pacman;
Position pacman2;
Position ghost1, ghost2, ghost3;
Direction dir = RIGHT;
Direction dir2 = RIGHT;
Mode game_mode = MODE_SINGLE;

int game_over = 0;
int score = 0;
int paused = 0;
int food_check[HEIGHT][WIDTH + 2];
int tick = 0;
int win_flag = 0;

int cherry_time = 0;
time_t cherry_start_time;
Point cherries[4] = { {1, 2}, {27, 2}, {1, 14}, {27, 14} };
int cherry_eaten[4] = {0, 0, 0, 0};
int remaining_food = 0;

volatile sig_atomic_t quit_flag = 0;
volatile sig_atomic_t pause_flag = 0;

char map[HEIGHT][WIDTH + 2];
Point ghost1_target, ghost2_target, ghost3_target;
int ghost1_has_target = 0, ghost2_has_target = 0, ghost3_has_target = 0;

// bfs
Point queue_buf[1000];
int front = 0, rear = 0;
void enqueue(Point p) { queue_buf[rear++] = p; }
Point dequeue() { return queue_buf[front++]; }
bool is_empty() { return front == rear; }
void reset_queue() { front = rear = 0; }

typedef struct {
    int fd;
    int player_id;
} Client;
Client clients[MAX_CLIENTS];

int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void sig_handler(int sig)
{
    if (sig == SIGINT) { quit_flag = 1; }
    else if (sig == SIGTSTP) { pause_flag = 1; }
}

void init_global_state()
{
    score = 0;
    game_over = 0;
    paused = 0;
    quit_flag = 0;
    pause_flag = 0;
    tick = 0;
    cherry_time = 0;
    for (int i = 0; i < 4; i++) cherry_eaten[i] = 0;
    remaining_food = 0;
    ghost1_has_target = 0;
    ghost2_has_target = 0;
    ghost3_has_target = 0;
}

void init_game()
{
    pacman.x = 14; pacman.y = 14; dir = DOWN;
    pacman2.x = 14; pacman2.y = 18; dir2 = DOWN;

    ghost1.x = 12; ghost1.y = 9;
    ghost2.x = 17; ghost2.y = 9;
    ghost3.x = 12; ghost3.y = 10;
}

//먹이
void init_food_count()
{
    remaining_food = 0;
    for (int i = 0; i < HEIGHT; i++)
        for (int j = 0; j < WIDTH; j++)
            if (food_check[i][j] == 1) remaining_food++;

    // 목표 클리어 개수로 덮어씀
    // remaining_food = WIN_FOOD_TARGET;
}


void input_map() {
    FILE *f = fopen("map.txt", "r");
    if (f == NULL) {
        perror("map.txt");
        exit(1);
    }
    for (int i = 0; i < HEIGHT; i++)
        if (fgets(map[i], sizeof(map[i]), f) == NULL) break;
    fclose(f);

    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH + 1; j++) {
            food_check[i][j] = 0;
            if (map[i][j] == '#') food_check[i][j] = 2;
            else if (map[i][j] == ' ') {
                if (((4 < i && i < 11) && (9 < j && j < 19)) || (i == 9 && !(j == 8 || j == 20)))
                    food_check[i][j] = 0;
                else
                    food_check[i][j] = 1;
            }
        }
    }
    init_food_count();
}

// 게임 로직
Point get_next_move_bfs(Point ghost, Point target) {
    if (ghost.x == target.x && ghost.y == target.y) return ghost;
    bool visited[HEIGHT][WIDTH];
    Point parent[HEIGHT][WIDTH];
    for (int i = 0; i < HEIGHT; i++) for (int j = 0; j < WIDTH; j++) visited[i][j] = false;

    reset_queue();
    enqueue(ghost);
    visited[ghost.y][ghost.x] = true;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    bool found = false;

    while (!is_empty()) {
        Point curr = dequeue();
        if (curr.x == target.x && curr.y == target.y) { found = true; break; }
        for (int i = 0; i < 4; i++) {
            int nx = curr.x + dx[i], ny = curr.y + dy[i];
            if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) continue;
            if (map[ny][nx] != '#' && map[ny][nx] != '_' &&
                map[ny][nx] != '-' && map[ny][nx] != '|' &&
                !visited[ny][nx]) {
                visited[ny][nx] = true;
                parent[ny][nx] = curr;
                Point np = {nx, ny};
                enqueue(np);
            }
        }
    }
    if (found) {
        Point curr = target;
        while (1) {
            Point prev = parent[curr.y][curr.x];
            if (prev.x == ghost.x && prev.y == ghost.y) return curr;
            curr = prev;
        }
    }
    return ghost;
}

int in_chase_range(Point ghost, Point pac)
{
    if (abs(ghost.x - pac.x) <= 2 && abs(ghost.y - pac.y) <= 2) return 1;
    return 0;
}

Point get_random_target()
{
    while (1)
    {
        int x = rand() % WIDTH;
        int y = rand() % HEIGHT;
        if (map[y][x] != '#' && map[y][x] != '_' && map[y][x] != '-' && map[y][x] != '|'){
            Point p = {x, y};
            return p;
        }
    }
}

// 체리 타임: 팩맨으로부터 가장 멀리 떨어진 곳을 고른다 (동률이면 랜덤 타이)
Point get_far_target_from_pac(Point pac) {
    int best_dist = -1;
    Point best = {pac.x, pac.y};
    int candidates = 0;
    int best_dist_far = -1;
    Point best_far = {pac.x, pac.y};
    int candidates_far = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (map[y][x] == '#' || map[y][x] == '_' || map[y][x] == '-' || map[y][x] == '|')
                continue;
            int dx = x - pac.x;
            int dy = y - pac.y;
            int dist = dx*dx + dy*dy;
            // 최소 거리 이상인 후보 우선
            if (dist >= FLEE_MIN_DIST2) {
                if (dist > best_dist_far) {
                    best_dist_far = dist;
                    best_far.x = x; best_far.y = y;
                    candidates_far = 1;
                } else if (dist == best_dist_far) {
                    candidates_far++;
                    if (rand() % candidates_far == 0) {
                        best_far.x = x; best_far.y = y;
                    }
                }
            }
            // 전체 최장 거리(폴백)
            if (dist > best_dist) {
                best_dist = dist;
                best.x = x; best.y = y;
                candidates = 1;
            } else if (dist == best_dist) {
                candidates++;
                if (rand() % candidates == 0) {
                    best.x = x; best.y = y;
                }
            }
        }
    }
    // 우선: 일정 거리 이상 떨어진 최장 후보, 없으면 전체 최장
    return (best_dist_far >= 0) ? best_far : best;
}

void try_eat_tile(int y, int x) {
    if (food_check[y][x] == 1) {
        score++;
        remaining_food--;
        food_check[y][x] = 0;
    }
}

Point get_chase_target_for(Point ghost_pos) {
    if (game_mode == MODE_SINGLE) {
        Point p = { pacman.x, pacman.y };
        return p;
    } else {
        Point p1 = { pacman.x,  pacman.y };
        Point p2 = { pacman2.x, pacman2.y };
        int d1 = (ghost_pos.x - p1.x)*(ghost_pos.x - p1.x) + (ghost_pos.y - p1.y)*(ghost_pos.y - p1.y);
        int d2 = (ghost_pos.x - p2.x)*(ghost_pos.x - p2.x) + (ghost_pos.y - p2.y)*(ghost_pos.y - p2.y);
        return (d1 <= d2) ? p1 : p2;
    }
}

int resolve_collision(Position *ghost, int respawn_x, int respawn_y, int *has_target) {
    // 1인용
    if (ghost->x == pacman.x && ghost->y == pacman.y) {
        if (cherry_time) {
            ghost->x = respawn_x;
            ghost->y = respawn_y;
            if (has_target) *has_target = 0;
        } else {
            game_over = 1;
        }
        return 1;
    }

    // 2dlsdyd
    if (game_mode == MODE_COUPLE &&
        ghost->x == pacman2.x && ghost->y == pacman2.y) {

        if (cherry_time) {
            ghost->x = respawn_x;
            ghost->y = respawn_y;
            if (has_target) *has_target = 0;
        } else {
            game_over = 1;
        }
        return 1;
    }

    return 0;
}

void move_player(Position* p, Direction dir)
{
    int next_x = p->x;
    int next_y = p->y;

    // 포탈 처리
    if (p->y == 9 && p->x == 1 && dir == LEFT) {
        next_x = 28;
    }
    else if (p->y == 9 && p->x == 28 && dir == RIGHT) {
        next_x = 1;
    }
    else {
        switch (dir) {
            case UP:    next_y--; break;
            case DOWN:  next_y++; break;
            case LEFT:  next_x--; break;
            case RIGHT: next_x++; break;
        }
    }

    if (next_x < 0 || next_x >= WIDTH || next_y < 0 || next_y >= HEIGHT)
        return;

    if (map[next_y][next_x] == ' ' || map[next_y][next_x] == '|') {
        p->x = next_x;
        p->y = next_y;
    }

    // p가 있는 자리에 먹이가 있으면 처리
    try_eat_tile(p->y, p->x);

    // p가 체리를 먹었는지 체크
    for (int k = 0; k < 4; k++) {
        if (!cherry_eaten[k] &&
            p->x == cherries[k].x &&
            p->y == cherries[k].y) {
            cherry_eaten[k] = 1;
            cherry_time = 1;
            time(&cherry_start_time);
        }
    }

    food_check[p->y][p->x] = 0;
}

void move_pacman() {
    if(game_mode == MODE_SINGLE)
    {
        move_player(&pacman, dir);
    }

    //2인용
    else if(game_mode == MODE_COUPLE)
    {
        move_player(&pacman, dir);
        move_player(&pacman2, dir2);
    }
}

void check_cherry_time() {
    if (cherry_time) {
        time_t now; time(&now);
        if (difftime(now, cherry_start_time) >= 10.0) cherry_time = 0;
    }
}

void ghost1_move() {
    resolve_collision(&ghost1, 12, 9, &ghost1_has_target);
    if (tick % 4 != 0) return; // 약간 빠르게
    Point ghost_pos = {ghost1.x, ghost1.y};
    Point pacman_pos = get_chase_target_for(ghost_pos);
    Point next_pos;
    if (cherry_time) {
        if (!ghost1_has_target || (ghost1.x == ghost1_target.x && ghost1.y == ghost1_target.y)) {
            ghost1_target = get_far_target_from_pac(pacman_pos);
            ghost1_has_target = 1;
        }
        next_pos = get_next_move_bfs(ghost_pos, ghost1_target);
    } else {
        if (in_chase_range(ghost_pos, pacman_pos)) next_pos = get_next_move_bfs(ghost_pos, pacman_pos);
        else {
            if (!ghost1_has_target || (ghost1.x == ghost1_target.x && ghost1.y == ghost1_target.y)) {
                ghost1_target = get_random_target();
                ghost1_has_target = 1;
            }
            next_pos = get_next_move_bfs(ghost_pos, ghost1_target);
        }
    }
    ghost1.x = next_pos.x; ghost1.y = next_pos.y;
}

void ghost2_move() {
    resolve_collision(&ghost2, 17, 9, &ghost2_has_target);
    if (tick % 4 != 1) return; // 약간 빠르게
    Point ghost_pos = {ghost2.x, ghost2.y};
    Point pacman_pos = get_chase_target_for(ghost_pos);
    Point next_pos;
    if (cherry_time) {
        if (!ghost2_has_target || (ghost2.x == ghost2_target.x && ghost2.y == ghost2_target.y)) {
            ghost2_target = get_far_target_from_pac(pacman_pos);
            ghost2_has_target = 1;
        }
        next_pos = get_next_move_bfs(ghost_pos, ghost2_target);
    } else {
        if (in_chase_range(ghost_pos, pacman_pos)) next_pos = get_next_move_bfs(ghost_pos, pacman_pos);
        else {
            if (!ghost2_has_target || (ghost2.x == ghost2_target.x && ghost2.y == ghost2_target.y)) {
                ghost2_target = get_random_target();
                ghost2_has_target = 1;
            }
            next_pos = get_next_move_bfs(ghost_pos, ghost2_target);
        }
    }
    ghost2.x = next_pos.x; ghost2.y = next_pos.y;
}

void ghost3_move() {
    resolve_collision(&ghost3, 12, 10, &ghost3_has_target);
    if (tick % 2 != 0) return;
    Point ghost_pos = {ghost3.x, ghost3.y};
    Point pacman_pos = get_chase_target_for(ghost_pos);
    Point next_pos;
    if (cherry_time) {
        if (!ghost3_has_target || (ghost3.x == ghost3_target.x && ghost3.y == ghost3_target.y)) {
            ghost3_target = get_far_target_from_pac(pacman_pos);
            ghost3_has_target = 1;
        }
        next_pos = get_next_move_bfs(ghost_pos, ghost3_target);
    } else {
        if (in_chase_range(ghost_pos, pacman_pos)) next_pos = get_next_move_bfs(ghost_pos, pacman_pos);
        else {
            if (!ghost3_has_target || (ghost3.x == ghost3_target.x && ghost3.y == ghost3_target.y)) {
                ghost3_target = get_random_target();
                ghost3_has_target = 1;
            }
            next_pos = get_next_move_bfs(ghost_pos, ghost3_target);
        }
    }
    ghost3.x = next_pos.x; ghost3.y = next_pos.y;
}

void check_win() {
    if (remaining_food <= 0) {
        game_over = 1;
        win_flag = 1;
    }
}

// 네트워크/입력 처리
void process_client_input(int player_id, int keycode) {
    if (game_mode == MODE_SINGLE) {
        switch (keycode) {
            case 'w': dir = UP;    break;
            case 's': dir = DOWN;  break;
            case 'a': dir = LEFT;  break;
            case 'd': dir = RIGHT; break;
            case 'p': pause_flag = 1; break;
            case 3:   quit_flag  = 1; break; // Ctrl+C
            default: break;
        }
    }

    else if (game_mode == MODE_COUPLE) {
        Direction *target_dir = NULL;

        if (player_id == 1) target_dir = &dir;
        else if (player_id == 2) target_dir = &dir2;
        if (!target_dir) return; // 이상한아이디 무시

        switch (keycode) {
            case 'w': *target_dir = UP;    break;
            case 's': *target_dir = DOWN;  break;
            case 'a': *target_dir = LEFT;  break;
            case 'd': *target_dir = RIGHT; break;
            case 'p': pause_flag = 1; break;
            case 3:   quit_flag  = 1; break;
            default: break;
        }
    }
}

void send_state_to_clients() {
    char buf[256];

    int p2x = pacman2.x;
    int p2y = pacman2.y;
    if(game_mode == MODE_SINGLE)
    {
        p2x = -1;
        p2y = -1;
    }

    int len = snprintf(buf, sizeof(buf),
                       "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                       tick,
                       pacman.x,  pacman.y,   // p1
                       p2x, p2y,  // p2
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

void send_state_to_client(int fd) {
    char buf[256];

    int p2x = pacman2.x;
    int p2y = pacman2.y;

    if (game_mode == MODE_SINGLE) {
        p2x = -1;
        p2y = -1;
    }


    int len = snprintf(buf, sizeof(buf),
                       "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                       tick,
                       pacman.x,  pacman.y,
                       p2x, p2y,
                       ghost1.x, ghost1.y,
                       ghost2.x, ghost2.y,
                       ghost3.x, ghost3.y,
                       score, remaining_food, cherry_time, paused);

    send(fd, buf, len, MSG_NOSIGNAL);
}

void run_server(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblock(listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, MAX_CLIENTS) < 0) { perror("listen"); exit(1); }

    for (int i = 0; i < MAX_CLIENTS; i++) { clients[i].fd = -1; clients[i].player_id = i + 1; }

    init_global_state();
    input_map();
    init_game();

    while (!game_over) {
        tick++;

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
        // 틱을 느리게 하기 위해 대기 시간 증가 (약 150ms)
        struct timeval tv = {0, 150000};
        int rv = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rv < 0 && errno != EINTR) perror("select");

        // accept
        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) {
                set_nonblock(cfd);
                int stored = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) {
                        clients[i].fd = cfd;
                        stored = 1;
                        break;
                    }
                }
                if (stored) {
                    send_state_to_client(cfd);
                } else {
                    const char *full_msg = "END ServerFull\n";
                    send(cfd, full_msg, strlen(full_msg), MSG_NOSIGNAL);
                    close(cfd);
                }
            }
        }

        // input
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0 && FD_ISSET(clients[i].fd, &rfds)) {
                char buf[64];
                ssize_t n = recv(clients[i].fd, buf, sizeof(buf), 0);
                if (n > 0) {
                    for (ssize_t k = 0; k < n; k++) process_client_input(clients[i].player_id, buf[k]);
                } else if (n == 0) {
                    close(clients[i].fd);
                    clients[i].fd = -1;
                } else { // n < 0
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        continue;
                    }
                    close(clients[i].fd);
                    clients[i].fd = -1;
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

    const char *end_msg = "END GameOver\n";

    if(win_flag) { end_msg = "END GameClear\n"; }

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0) send(clients[i].fd, end_msg, strlen(end_msg), 0);
    close(listen_fd);
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    int m = 0;

    printf("1: single play\n");
    printf("2: 2 person play\n");
    printf("select: ");
    if (scanf("%d", &m) != 1) return 1;

    if (m == 1) game_mode = MODE_SINGLE;
    else if (m == 2) game_mode = MODE_COUPLE;
    else {
        printf("Invalid mode\n");
        return 1;
    }

    srand(time(NULL));
    signal(SIGINT, sig_handler);
    signal(SIGTSTP, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    run_server(port);

    return 0;
}