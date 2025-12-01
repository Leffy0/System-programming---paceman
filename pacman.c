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

volatile sig_atomic_t quit_flag = 0;
volatile sig_atomic_t pause_flag = 0;

// 기본 맵 (나중에 파일 입력으로 바꿔도 가능)
char map[HEIGHT][WIDTH+2];

int px = 2, py = 2;  // 팩맨 초기 위치

// 팩맨까지 가는 최단 경로의 '다음 이동 좌표'를 반환하는 함수
Point get_next_move_bfs(Point ghost, Point target) {
    // 1. 방문 여부와 경로 추적을 위한 배열 초기화
    bool visited[HEIGHT][WIDTH];
    Point parent[HEIGHT][WIDTH]; // "어디서 왔는지" 족보를 기록

    // 초기화
    for(int i=0; i<HEIGHT; i++) {
        for(int j=0; j<WIDTH; j++) {
            visited[i][j] = false;
        }
    }

    // 2. BFS 시작 (유령 위치에서 출발)
    reset_queue();
    enqueue(ghost);
    visited[ghost.y][ghost.x] = true;

    // 상하좌우 탐색용 배열
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    bool found = false;

    while (!is_empty()) {
        Point curr = dequeue();

        // 팩맨을 찾았으면 중단
        if (curr.x == target.x && curr.y == target.y) {
            found = true;
            break;
        }

        // 상하좌우 4방향 확인
        for (int i = 0; i < 4; i++) {
            int nx = curr.x + dx[i];
            int ny = curr.y + dy[i];

            // 맵 범위 체크
            if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) continue;

            // 벽이 아니고(#), 아직 안 가본 곳이고, 유령집 문(-)도 아니면 이동 가능
            // (유령이 문을 통과하게 하려면 && map[ny][nx] != '-' 제거)
            if (map[ny][nx] != '#' && !visited[ny][nx]) {
                visited[ny][nx] = true;
                parent[ny][nx] = curr; // "curr에서 왔음" 기록
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

void draw_food()
{
    for(int i = 0; i < HEIGHT; i++)
        for(int j = 0; j < WIDTH + 1; j++)
            if(food_check[i][j] == 1) mvprintw(i, j, ".");
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


void init_game(){
    initscr();          //Initialize ncurses mode
    noecho();           //Disable automatioc echoing of typed characters
    curs_set(FALSE);    //Hide the cursor
    timeout(100);       //Set input timeout(non-blocking getch)
    srand(time(NULL));  //Seed the random number generator
    signal(SIGINT, sig_handler);
    signal(SIGTSTP, sig_handler);

    pacman.x = 14;
    pacman.y = 14;
    dir = RIGHT;

    ghost1.x = 12;
    ghost1.y = 10;
    ghost1_dir = 0;
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

void ghost1_move()
{
    // 속도 조절 (예: 3틱마다 1번 이동 - 좀 더 빠르게 쫓아오게 하려면 숫자를 줄이세요)
    if(tick % 5 != 0) {
        mvprintw(ghost1.y, ghost1.x, "A");
        return;
    }

    Point ghost_pos = {ghost1.x, ghost1.y};
    Point pacman_pos = {pacman.x, pacman.y};

    // [고지능] BFS로 다음 이동할 좌표 계산
    Point next_pos = get_next_move_bfs(ghost_pos, pacman_pos);

    // 이동 적용
    ghost1.x = next_pos.x;
    ghost1.y = next_pos.y;

    // 충돌 체크
    if(ghost1.x == pacman.x && ghost1.y == pacman.y) game_over = 1;

    // 유령 그리기
    mvprintw(ghost1.y, ghost1.x, "A");
}

int main() {
    input_map();
    init_game();
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    timeout(100);   // non-blocking 입력

    while(!game_over) 
    {
        tick = !tick;
        draw_map();
        if (quit_flag) {
            mvprintw(HEIGHT / 2, (WIDTH / 2) - 14, "Are you sure you want to quit? (y/n):");
            refresh();
            timeout(-1); // 입력을 무한정 기다림 (blocking)
            char c = getch();
            timeout(100); // 다시 원래대로 (non-blocking)
            flushinp(); // 입력 버퍼 비우기
            if (c == 'y' || c == 'Y') {
                end_game("Terminated by user.");
            }
            quit_flag = 0; // 'n'을 눌렀으면 플래그를 리셋하고 게임 계속
        }
        if (pause_flag) {
            paused = !paused;
            pause_flag = 0; // 플래그 처리 후 즉시 리셋
        }
        if(paused){
        clear();
        draw_pacman();
        ghost1_move();
        draw_status();
        mvprintw(HEIGHT / 2, (WIDTH / 2) - 5, "== PAUSED ==");
        mvprintw(HEIGHT / 2 + 1, (WIDTH / 2) - 12, "Press 'p' or Ctrl+Z to resume");
        refresh();
        handle_input(); // 일시정지 중에도 'p' 키나 Ctrl+Z로 해제 가능하도록
        continue; // 일시정지 상태에서는 아래 로직 실행 안 함
        
    }
        clear();
        draw_map();
        draw_pacman();
        ghost1_move();
        draw_status();
        handle_input();     //Process user input

        if(!paused){
            move_pacman();
        }

        refresh();
        usleep(80000);
    }

    end_game("Game Over!");
    return 0;
}
