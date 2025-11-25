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


typedef struct 
{
    int x,y;
}Position;

typedef enum {UP, DOWN, LEFT, RIGHT} Direction;

Position pacman;
Direction dir = RIGHT;
Position food;

int game_over = 0;
int score = 0;
int paused = 0;

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
    for(int i=0;i<HEIGHT;i++)
        mvprintw(i, 0, "%s", map[i]);

    }void draw_status(){
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
    //TODO: Implement snake drawing
    mvprintw(pacman.y, pacman.x,"P");
   // 벽 충돌 확인
    if (map[pacman.y][pacman.x] != '#') {
        px = pacman.x;
        py = pacman.y;
    }
}void move_pacman() {
    int next_x = pacman.x;
    int next_y = pacman.y;

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
    if (map[next_y][next_x] != '#') {
        pacman.x = next_x;
        pacman.y = next_y;
        
        // px, py는 현재 코드에서 출력용으로는 안 쓰이지만, 동기화가 필요하다면 업데이트
        px = pacman.x;
        py = pacman.y;
    }
    // else: 벽이라면 좌표를 갱신하지 않으므로 제자리에 멈춤
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
    initscr();
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
        }

        refresh();
        usleep(80000);
    }

    end_game("Game Over!");
    return 0;
}
