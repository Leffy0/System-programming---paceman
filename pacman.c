#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

#define HEIGHT 20
#define WIDTH 29

void init_game();
void sig_handler(int sig);
void draw_status();
void draw_pacman();
void move_pacman();
void handle_input();
void end_game(const char *message);

typedef struct { int x, y; } Point;

Point queue[1000];
int front = 0;
int rear = 0;

void enqueue(Point p) { queue[rear++] = p; }

Point dequeue() { return queue[front++]; }

bool is_empty() { return front == rear; }

void reset_queue() { front = rear = 0; }

typedef struct
{ int x,y; }Position;

typedef enum {UP, DOWN, LEFT, RIGHT} Direction;

Position pacman;
Position ghost1;
int ghost1_dir = 0;
Position ghost2;
int ghost2_dir = 0;
Position ghost3;
int ghost3_dir = 0;
Position ghost4;
int ghost4_dir = 0;

Direction dir = RIGHT;
Position food;

int game_over = 0;
int score = 0;
int paused = 0;
int food_check[HEIGHT][WIDTH + 2];
int tick = 0;

int cherry_time = 0;
time_t cherry_start_time;
Point cherries[4] = { {1, 2}, {27, 2}, {1, 14}, {27, 14} };
int cherry_eaten[4] = {0, 0, 0, 0};

volatile sig_atomic_t quit_flag = 0;
volatile sig_atomic_t pause_flag = 0;

char map[HEIGHT][WIDTH+2];

int px = 2, py = 2;

Point ghost1_target;
int ghost1_has_target = 0;
Point ghost2_target;
int ghost2_has_target = 0;

Point ghost3_target;
int ghost3_has_target = 0;
Point ghost4_target;
int ghost4_has_target = 0;


Point get_next_move_bfs(Point ghost, Point target) {
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
            if (map[i][j] == '#') food_check[i][j] = 2;
            else if (map[i][j] == ' ')
            {
                if(((4 < i && i < 11) && (9 < j && j < 19)) || (i == 9 && !(j == 8 || j == 20)))
                { food_check[i][j] = 0; }
                else { food_check[i][j] = 1; }
            }
        }
    }
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

void draw_status()
{
    mvprintw(HEIGHT, 0, "Score: %d", score);
    mvprintw(HEIGHT+1, 0, "Move: WASD | Quit: Ctrl+C | Pause: P");
}

void sig_handler(int sig) {
    if (sig == SIGINT) {
        quit_flag = 1;
    } else if (sig == SIGTSTP) {
        // pause_flag를 토글
        pause_flag = 1;
    }
}

void init_game() {
    pacman.x = 14;
    pacman.y = 14;
    dir = DOWN;

    ghost1.x = 12;
    ghost1.y = 9;
    ghost1_dir = 0;

    ghost2.x = 17;
    ghost2.y = 9;
    ghost2_dir = 1;

    ghost3.x = 12;
    ghost3.y = 10;
    ghost3_dir = 2;

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

    if(food_check[py][px] == 1) score++;

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


void handle_input(){
    int ch = getch();
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

void ghost1_move()
{
    // 충돌 체크
    if (ghost1.x == pacman.x && ghost1.y == pacman.y)
    {
        if (cherry_time)
        {
            // 먹혔으면 리스폰
            ghost1.x = 12;
            ghost1.y = 9;
            ghost1_has_target = 0;
        }
        else {
            game_over = 1;
        }
    }

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

    // 유령 그리기 (색/문자는 draw_ghost가 처리)
    draw_ghost(ghost1);
}

void ghost2_move()
{
    if (ghost2.x == pacman.x && ghost2.y == pacman.y)
    {
        if (cherry_time)
        {
            ghost2.x = 17;
            ghost2.y = 9;
            ghost2_has_target = 0;
        }
        else {
            game_over = 1;
        }
    }

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

    draw_ghost(ghost2);
}

void ghost3_move()
{
    if (ghost3.x == pacman.x && ghost3.y == pacman.y)
    {
        if (cherry_time)
        {
            ghost3.x = 12;
            ghost3.y = 10;
            ghost3_has_target = 0;
        }
        else {
            game_over = 1;
        }
    }

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

int main() {
    input_map();

    initscr();
    cbreak();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);

    timeout(50);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_RED,  COLOR_BLACK); // 평소 유령
        init_pair(2, COLOR_CYAN, COLOR_BLACK); // 체리타임 유령
    }

    srand(time(NULL));
    signal(SIGINT,  sig_handler); // 종료 시그널
    signal(SIGTSTP, sig_handler); // 일시정지 시그널

    init_game();   // non-blocking 입력

    while(!game_over)
    {
        tick = !tick;
        draw_map();
        if (quit_flag) {
            mvprintw(HEIGHT / 2, (WIDTH / 2) - 14, "Are you sure you want to quit? (y/n):");
            refresh();
            timeout(-1);
            char c = getch();
            timeout(100);
            flushinp();
            if (c == 'y' || c == 'Y') { end_game("Terminated by user."); }
            quit_flag = 0;
        }
        if (pause_flag) {
            paused = !paused;
            pause_flag = 0;
        }
        if(paused){
            clear();
            draw_pacman();
            ghost1_move();
            ghost2_move();
            ghost3_move();
            draw_status();
            mvprintw(HEIGHT / 2, (WIDTH / 2) - 5, "== PAUSED ==");
            mvprintw(HEIGHT / 2 + 1, (WIDTH / 2) - 12, "Press 'p' or Ctrl+Z to resume");
            refresh();
            handle_input(); // 일시정지 해제
            continue;

        }
        clear();
        draw_map();
        draw_pacman();
        ghost1_move();
        ghost2_move();
        ghost3_move();
        draw_status();
        check_cherry_time();
        handle_input();

        if(!paused){
            move_pacman();
        }

        if(score == 257) game_over = 1;

        refresh();
        usleep(80000);
    }

    end_game("Game Over!");
    return 0;
}