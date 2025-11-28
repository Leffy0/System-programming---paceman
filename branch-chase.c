#include <curses.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#define WIDTH 50
#define HEIGHT 50

typedef struct
{
    int x;
    int y;
} position;

typedef enum {up, down, left, right} direction;

int game_over = 0;
position pacman;
position ghost1;
direction dir = right;
int mouse = 0;
int temp = 0;
int food_arr[WIDTH][HEIGHT];

void init_game()
{
    initscr();
    cbreak();

    noecho();
    curs_set(FALSE);
    timeout(100);
    srand(time(NULL));

    pacman.x = 1;
    pacman.y = 1;

    ghost1.x = WIDTH / 2;
    ghost1.y = HEIGHT / 2;
}

void move_pacman()
{
    switch (dir)
    {
        case up:    pacman.y--; break;
        case down:  pacman.y++; break;
        case left:  pacman.x--; break;
        case right: pacman.x++; break;
    }

    if(pacman.x < 1 && dir == left) pacman.x = WIDTH - 1;
    if(pacman.x > WIDTH - 1 && dir == right) pacman.x = 1;
    if(pacman.y < 1 && dir == up) pacman.y = HEIGHT - 1;
    if(pacman.y > HEIGHT - 1 && dir == down) pacman.y = 1;

    food_arr[pacman.x][pacman.y] = 0;
    return;
}

void input()
{
    int ch  = getch();

    switch (ch)
    {
        case 'w' : dir = up; break;
        case 's' : dir = down; break;
        case 'a' : dir = left; break;
        case 'd' : dir = right; break;
        case 'p' : game_over = 1; break;
    }
}

void draw_pacman()
{
    static int frame = 0;
    frame++;
    mouse = frame % 2;
    mvprintw(pacman.y, pacman.x, mouse ? "c" : "C");
}

void draw_food()
{
    for(int i = 1; i < WIDTH; i++) for(int j = 0; j < HEIGHT; j++)
            if(food_arr[i][j]) mvprintw(j, i, ".");
}

void chase_pacman()
{
    if(ghost1.x > pacman.x) temp = 1;
    else if (ghost1.x < pacman.x) temp = 0;
    else
    {
        if(ghost1.y > pacman.y) temp = 3;
        else if (ghost1.y < pacman.y) temp = 2;
    }
}

void move_ghost()
{

    if((abs(ghost1.x - pacman.x) <= 10) && (abs(ghost1.y - pacman.y) <= 10)) { chase_pacman(); }

    else
    {
        static int tick = 0;
        tick++;
        if (tick % 5 == 0) temp = rand() % 4;
    }

    switch (temp) {
        case 0 :
            ghost1.x++;
            break; // R
        case 1 :
            ghost1.x--;
            break; // L
        case 2 :
            ghost1.y++;
            break; // U
        case 3 :
            ghost1.y--;
            break; // D
    }

    mvprintw(ghost1.y, ghost1.x, "A");

    if (ghost1.x < 1) ghost1.x = WIDTH - 1;
    if (ghost1.x > WIDTH - 1) ghost1.x = 1;
    if (ghost1.y < 1) ghost1.y = HEIGHT - 1;
    if (ghost1.y > HEIGHT - 1) ghost1.y = 1;

    if (pacman.x == ghost1.x && pacman.y == ghost1.y) game_over = 1;

}

int main()
{
    init_game();

    for(int i = 0; i < WIDTH; i++)
        for(int j = 0; j < HEIGHT; j++)
            food_arr[i][j] = 1;

    while (!game_over)
    {
        clear();
        draw_pacman();
        draw_food();

        input();
        move_pacman();
        move_ghost();

        refresh();
        usleep(100000);
    }

    endwin();
    return 0;
}
