#include <ncurses.h>
#include <unistd.h>

#define H 20
#define W 40

// 기본 맵 (나중에 파일 입력으로 바꿔도 가능)
char map[H][W+1] = {
    "########################################",
    "#......................##...............",
    "#.####.######.#######..##..#############",
    "#........................................",
    "##############....###############.......",
    "#........................................",
    "#.##########....##########..............",
    "#........................................",
    "########################################"
};

int px = 2, py = 2;  // 팩맨 초기 위치

void draw_map() {
    for(int i=0;i<H;i++)
        mvprintw(i, 0, "%s", map[i]);
}

int main() {
    initscr();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(100);   // non-blocking 입력

    while(1) {
        clear();
        draw_map();
        mvaddch(py, px, 'P'); // 팩맨

        refresh();

        int ch = getch();
        int nx = px, ny = py;

        if (ch == 'q') break; // 종료
        if (ch == KEY_UP || ch == 'w') ny--;
        if (ch == KEY_DOWN || ch == 's') ny++;
        if (ch == KEY_LEFT || ch == 'a') nx--;
        if (ch == KEY_RIGHT || ch == 'd') nx++;

        // 벽 충돌 확인
        if (map[ny][nx] != '#') {
            px = nx;
            py = ny;
        }

        usleep(80000);
    }

    endwin();
    return 0;
}
