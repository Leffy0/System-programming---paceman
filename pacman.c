#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define HEIGHT 20
#define WIDTH 40

void init_game();
void sig_handler(int sig);
void draw_status();
void draw_pacman();
void move_pacman();
void handle_input();
void end_game(const char *message);
int can_move(int y,int x);
int is_wall(int y,int x);
int near_pacman();


typedef struct 
{
    int x,y;
}Position;

typedef enum {UP, DOWN, LEFT, RIGHT} Direction;

typedef enum {G_UP, G_DOWN, G_LEFT, G_RIGHT} GhostDir;
GhostDir ghost_dir = G_RIGHT;

Position pacman;
Position ghost1;
Direction dir = RIGHT;
Position food;

int game_over = 0;
int score = 0;
int paused = 0;
int mouse = 0;

volatile sig_atomic_t quit_flag = 0;
volatile sig_atomic_t pause_flag = 0;

// 기본 맵 (나중에 파일 입력으로 바꿔도 가능)
char map[HEIGHT][WIDTH+1] = {
    "########################################",
    "#......................##..............#",
    "#.####.######.#######..##..#############",
    "#.......................................#",
    "##############....###############.......#",
    "#.......................................#",
    "#.##########....##########..............#",
    "#.......................................#",
    "########################################"
};

int px = 2, py = 2;  // 팩맨 초기 위치

void draw_map() {
    for(int i=0;i<HEIGHT;i++){
        mvprintw(i, 0, "%s", map[i]);
    }
}

void draw_status(){
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

int can_move(int y, int x) {
    return !is_wall(y, x);
}

int is_wall(int y, int x) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return 1;
    return map[y][x] == '#';
}

int near_pacman() {
    return abs(ghost1.x - pacman.x) + abs(ghost1.y - pacman.y) <= 8;
}


void init_game(){
    ghost1.x = 20;
    ghost1.y = 5;
    ghost_dir = G_RIGHT;
    initscr();          //Initialize ncurses mode
    noecho();           //Disable automatioc echoing of typed characters
    curs_set(FALSE);    //Hide the cursor
    timeout(100);       //Set input timeout(non-blocking getch)
    srand(time(NULL));  //Seed the random number generator
    signal(SIGINT, sig_handler);
    signal(SIGTSTP, sig_handler);

    pacman.x = 2; 
    pacman.y = 2;
    dir = RIGHT;

}

void end_game(const char *message){
    endwin(); 
    printf("%s Final Score: %d\n", message, score);
    exit(0); 
}

void draw_pacman(){

    static int frame = 0;
    frame++;
    mouse = frame % 2;
    mvprintw(pacman.y, pacman.x, mouse ? "c" : "C");

    // 벽 충돌 확인
    if (map[pacman.y][pacman.x] != '#') {
        px = pacman.x;
        py = pacman.y;
    }
}

void move_pacman() {
    int next_x = pacman.x;
    int next_y = pacman.y;

    // 1. 방향에 따라 다음 좌표 계산
    switch (dir) {
        case UP:    next_y--; break;
        case DOWN:  next_y++; break;
        case LEFT:  next_x--; break;
        case RIGHT: next_x++; break;
    }

    // 2. 맵 범위 체크
    if (next_x < 0 || next_x >= WIDTH || next_y < 0 || next_y >= HEIGHT) {
        return;
    }

    // 3. 벽이면 그냥 리턴
    if (map[next_y][next_x] == '#') {
        return;
    }

    // 4. 먹이(.)면 점수 올리고 맵에서 지우기
    if (map[next_y][next_x] == '.') {
        score++;
        map[next_y][next_x] = ' ';   // 먹은 자리는 빈 칸으로
    }

    // 5. 실제 위치 갱신
    pacman.x = next_x;
    pacman.y = next_y;
}

void move_ghost()
{
    int next_x = ghost1.x;
    int next_y = ghost1.y;

    int moved = 0;

    // 1) 추적 모드
    if (near_pacman()) {

        int candidates[4][2] = {
            {ghost1.y - 1, ghost1.x},     // UP
            {ghost1.y + 1, ghost1.x},     // DOWN
            {ghost1.y, ghost1.x - 1},     // LEFT
            {ghost1.y, ghost1.x + 1}      // RIGHT
        };

        int best_dir = -1;
        int best_dist = 999999;

        for (int i = 0; i < 4; i++) {
            int ny = candidates[i][0];
            int nx = candidates[i][1];

            if (!can_move(ny, nx)) continue; // 벽이면 스킵

            int dist = abs(nx - pacman.x) + abs(ny - pacman.y);
            if (dist < best_dist) {
                best_dist = dist;
                best_dir = i;
            }
        }

        if (best_dir != -1) {
            ghost_dir = best_dir;
        }
    }
    // 2) 아니면 랜덤 이동 (가끔 방향 전환)
    else {
        if (rand() % 8 == 0) {
            ghost_dir = rand() % 4;
        }
    }

    // 3) 실제 이동 (벽이면 다른 방향 선택)
    for (int i = 0; i < 4; i++) {
        switch (ghost_dir) {
            case G_UP:    next_y = ghost1.y - 1; next_x = ghost1.x; break;
            case G_DOWN:  next_y = ghost1.y + 1; next_x = ghost1.x; break;
            case G_LEFT:  next_y = ghost1.y; next_x = ghost1.x - 1; break;
            case G_RIGHT: next_y = ghost1.y; next_x = ghost1.x + 1; break;
        }

        if (can_move(next_y, next_x)) {
            ghost1.x = next_x;
            ghost1.y = next_y;
            moved = 1;
            break;
        }

        // 벽이면 방향만 랜덤으로 바꿔서 다시 시도
        ghost_dir = rand() % 4;
    }

    // 4) 그리기
    mvprintw(ghost1.y, ghost1.x, "A");

    // 5) 팩맨 충돌 체크
    if (pacman.x == ghost1.x && pacman.y == ghost1.y)
        game_over = 1;
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

int main() {
    init_game();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    timeout(100);   // non-blocking 입력    

    while(!game_over) 
    {
        if (quit_flag) {
            mvprintw(HEIGHT / 2, (WIDTH / 2) - 15, "Are you sure you want to quit? (y/n):");
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
        draw_map();
        draw_pacman();
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
        draw_status();
        handle_input();     //Process user input

        if(!paused){
            move_pacman();
            move_ghost();
        }

        refresh();
        usleep(80000);
    }

    end_game("Game Over!");
    return 0;
}
