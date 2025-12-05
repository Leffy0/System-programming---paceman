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

#define HEIGHT 20
#define WIDTH 29

#define MAX_CLIENTS 2
#define DEFAULT_PORT 5000

typedef struct { int x, y; } Point;
typedef struct { int x, y; } Position;
typedef enum { UP, DOWN, LEFT, RIGHT } Direction;

// 전역 상태
Position pacman;
Position ghost1, ghost2, ghost3;
Direction dir = RIGHT;
int game_over = 0;
int score = 0;
int paused = 0;
int food_check[HEIGHT][WIDTH + 2];
int tick = 0;
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

// 큐 (BFS)
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

// 유틸/초기화
int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void sig_handler(int sig) {
    if (sig == SIGINT) {
        quit_flag = 1;
    } else if (sig == SIGTSTP) {
        pause_flag = 1;
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
    ghost1_has_target = 0;
    ghost2_has_target = 0;
    ghost3_has_target = 0;
}

void init_game() {
    pacman.x = 14; pacman.y = 14; dir = DOWN;
    ghost1.x = 12; ghost1.y = 9;
    ghost2.x = 17; ghost2.y = 9;
    ghost3.x = 12; ghost3.y = 10;
}

// 맵/먹이
void init_food_count() {
    remaining_food = 0;
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            if (food_check[i][j] == 1) remaining_food++;
        }
    }
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

int in_chase_range(Point ghost, Point pac) {
    if (abs(ghost.x - pac.x) <= 2 && abs(ghost.y - pac.y) <= 2) return 1;
    return 0;
}

Point get_random_target() {
    while (1) {
        int x = rand() % WIDTH;
        int y = rand() % HEIGHT;
        if (map[y][x] != '#' && map[y][x] != '_' && map[y][x] != '-' && map[y][x] != '|') {
            Point p = {x, y};
            return p;
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

int resolve_collision(Position *ghost, int respawn_x, int respawn_y, int *has_target) {
    if (ghost->x != pacman.x || ghost->y != pacman.y) return 0;
    if (cherry_time) {
        ghost->x = respawn_x; ghost->y = respawn_y;
        if (has_target) *has_target = 0;
    } else {
        game_over = 1;
    }
    return 1;
}

void move_pacman() {
    int next_x = pacman.x, next_y = pacman.y;
    if (pacman.x == 1 && pacman.y == 9) next_x = 28;
    if (pacman.x == 28 && pacman.y == 9) next_x = 1;
    switch (dir) {
        case UP: next_y--; break;
        case DOWN: next_y++; break;
        case LEFT: next_x--; break;
        case RIGHT: next_x++; break;
    }
    if (next_x < 0 || next_x >= WIDTH || next_y < 0 || next_y >= HEIGHT) return;
    if (map[next_y][next_x] == ' ' || map[next_y][next_x] == '|') {
        pacman.x = next_x; pacman.y = next_y;
    }
    try_eat_tile(pacman.y, pacman.x);
    for (int k = 0; k < 4; k++) {
        if (!cherry_eaten[k] && pacman.x == cherries[k].x && pacman.y == cherries[k].y) {
            cherry_eaten[k] = 1;
            cherry_time = 1;
            time(&cherry_start_time);
        }
    }
    food_check[pacman.y][pacman.x] = 0;
}

void check_cherry_time() {
    if (cherry_time) {
        time_t now; time(&now);
        if (difftime(now, cherry_start_time) >= 10.0) cherry_time = 0;
    }
}

void ghost1_move() {
    resolve_collision(&ghost1, 12, 9, &ghost1_has_target);
    if (tick % 5 != 0) return;
    Point ghost_pos = {ghost1.x, ghost1.y};
    Point pacman_pos = {pacman.x, pacman.y};
    Point next_pos;
    if (cherry_time) {
        if (!ghost1_has_target || (ghost1.x == ghost1_target.x && ghost1.y == ghost1_target.y)) {
            ghost1_target = get_random_target();
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
    if (tick % 5 != 1) return;
    Point ghost_pos = {ghost2.x, ghost2.y};
    Point pacman_pos = {pacman.x, pacman.y};
    Point next_pos;
    if (cherry_time) {
        if (!ghost2_has_target || (ghost2.x == ghost2_target.x && ghost2.y == ghost2_target.y)) {
            ghost2_target = get_random_target();
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
    if (tick != 0) return;
    Point ghost_pos = {ghost3.x, ghost3.y};
    Point pacman_pos = {pacman.x, pacman.y};
    Point next_pos;
    if (cherry_time) {
        if (!ghost3_has_target || (ghost3.x == ghost3_target.x && ghost3.y == ghost3_target.y)) {
            ghost3_target = get_random_target();
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
    }
}

// 네트워크/입력 처리
void process_client_input(int player_id, int keycode) {
    (void)player_id; // P1만
    switch (keycode) {
        case 'w': dir = UP; break;
        case 's': dir = DOWN; break;
        case 'a': dir = LEFT; break;
        case 'd': dir = RIGHT; break;
        case 'p': pause_flag = 1; break;
        case 3:   quit_flag = 1; break; // Ctrl+C
        default: break;
    }
}

void send_state_to_clients() {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
        tick, pacman.x, pacman.y,
        ghost1.x, ghost1.y,
        ghost2.x, ghost2.y,
        ghost3.x, ghost3.y,
        score, remaining_food, cherry_time, paused);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) send(clients[i].fd, buf, len, 0);
    }
}

void send_state_to_client(int fd) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
        tick, pacman.x, pacman.y,
        ghost1.x, ghost1.y,
        ghost2.x, ghost2.y,
        ghost3.x, ghost3.y,
        score, remaining_food, cherry_time, paused);
    send(fd, buf, len, 0);
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
        struct timeval tv = {0, 80000};
        int rv = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rv < 0 && errno != EINTR) perror("select");

        // accept
        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) {
                set_nonblock(cfd);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) { clients[i].fd = cfd; break; }
                }
                send_state_to_client(cfd);
            }
        }

        // input
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0 && FD_ISSET(clients[i].fd, &rfds)) {
                char buf[64];
                ssize_t n = recv(clients[i].fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    close(clients[i].fd); clients[i].fd = -1;
                } else {
                    for (ssize_t k = 0; k < n; k++) process_client_input(clients[i].player_id, buf[k]);
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
    for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i].fd >= 0) send(clients[i].fd, end_msg, strlen(end_msg), 0);
    close(listen_fd);
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    srand(time(NULL));
    signal(SIGINT, sig_handler);
    signal(SIGTSTP, sig_handler);
    run_server(port);
    return 0;
}
