#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define HEIGHT 20
#define WIDTH 29

// 기본 접속 정보 (필요에 따라 수정)
#define IP   "192.168.64.3"
#define PORT 5000

// 서버→클라이언트 게임 상태 패킷 구조 (클라에서 렌더링용)
typedef struct {
    // 맵 문자열 (서버가 전송하지 않으면 클라가 직접 map.txt를 읽어 채워야 함)
    char map[HEIGHT][WIDTH + 1];

    int p1_x, p1_y;
    int p2_x, p2_y;
    int g1_x, g1_y;
    int g2_x, g2_y;
    int g3_x, g3_y;

    int score;
    int remaining_food;
    int cherry_time;
    int paused;
    int game_over;
} GameState;

// 클라이언트→서버 입력 패킷
typedef struct {
    int key;        // 원본 키 코드
    int player_id;  // 1 또는 2
} InputPacket;

#endif // COMMON_H
