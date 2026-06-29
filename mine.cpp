#include <raylib.h>
#include <raymath.h>

// Константы мыши
const float MOUSE_SENSE = 0.003f;
const float MOUSE_UP_LIMIT = 1.5f;
const float MOUSE_DOWN_LIMIT = -1.5f;

// Константы персонажа
const float HERO_SPEED = 10.0f;

// Константы мира
const int WORLD_SIZE = 16;

class World
{
public:
    int map[WORLD_SIZE][WORLD_SIZE][WORLD_SIZE] = {};

    void Init() {
        for (int x = 0; x < WORLD_SIZE; x++)
            for (int z = 0; z < WORLD_SIZE; z++)
                map[x][0][z] = 1;
    }
};

class Hero
{
public:
    Vector3 pos;
    float yaw   = 0.0f;
    float pitch = 0.0f;
    float speed = HERO_SPEED;

    Vector3 GetForward() {
        return { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
    }

    Vector3 GetRight() {
        return { sinf(yaw - PI/2), 0.0f, cosf(yaw - PI/2) };
    }

    void Update(Vector3 forward, Vector3 right, float dt) {
        if (IsKeyDown(KEY_W)) pos = Vector3Add(pos, Vector3Scale(forward, speed * dt));
        if (IsKeyDown(KEY_S)) pos = Vector3Subtract(pos, Vector3Scale(forward, speed * dt));
        if (IsKeyDown(KEY_A)) pos = Vector3Subtract(pos, Vector3Scale(right, speed * dt));
        if (IsKeyDown(KEY_D)) pos = Vector3Add(pos, Vector3Scale(right, speed * dt));
    }

    void Rotate(Vector2 mouse) {
        yaw   -= mouse.x * MOUSE_SENSE;
        pitch -= mouse.y * MOUSE_SENSE;

        if (pitch >  MOUSE_UP_LIMIT)   pitch =  MOUSE_UP_LIMIT;
        if (pitch <  MOUSE_DOWN_LIMIT)  pitch =  MOUSE_DOWN_LIMIT;
    }
};

int main() {
    InitWindow(GetMonitorWidth(0), GetMonitorHeight(0), "Mine");
    SetWindowState(FLAG_WINDOW_UNDECORATED);
    DisableCursor();
    SetTargetFPS(165);

    Hero hero;
    hero.pos = { 8.0f, 3.0f, 8.0f }; // стартуем над картой, по центру

    Camera3D camera = {};
    camera.up         = { 0.0f, 1.0f, 0.0f };
    camera.fovy       = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    World world;
    world.Init();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Поворот и движение
        hero.Rotate(GetMouseDelta());
        Vector3 forward = hero.GetForward();
        Vector3 right   = hero.GetRight();
        hero.Update(forward, right, dt);

        // Камера следует за героем
        camera.position = hero.pos;
        camera.target   = Vector3Add(hero.pos, forward);

        // Рейкаст из центра экрана
        Ray ray = GetMouseRay(
            { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f },
                              camera
        );

        bool hit = false;
        int hx = 0, hy = 0, hz = 0;

        const float STEP = 0.05f;
        const float MAX_DIST = 8.0f;

        for (float t = 0; t < MAX_DIST; t += STEP) {
            Vector3 point = Vector3Add(ray.position, Vector3Scale(ray.direction, t));

            int x = (int)floorf(point.x);
            int y = (int)floorf(point.y);
            int z = (int)floorf(point.z);

            if (x < 0 || y < 0 || z < 0 || x >= WORLD_SIZE || y >= WORLD_SIZE || z >= WORLD_SIZE)
                continue;

            if (world.map[x][y][z] == 1) {
                hit = true;
                hx = x; hy = y; hz = z;
                break;
            }
        }

        // --- Отрисовка ---
        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);

        // Блоки мира
        for (int x = 0; x < WORLD_SIZE; x++)
            for (int y = 0; y < WORLD_SIZE; y++)
                for (int z = 0; z < WORLD_SIZE; z++)
                    if (world.map[x][y][z] == 1)
                        DrawCube(
                            { x + 0.5f, y + 0.5f, z + 0.5f },
                            1.0f, 1.0f, 1.0f, RED
                        );

                    // Подсветка выделенного блока
                    if (hit)
                        DrawCubeWires(
                            { hx + 0.5f, hy + 0.5f, hz + 0.5f },
                            1.01f, 1.01f, 1.01f, WHITE
                        );

                    DrawGrid(100, 1.0f);

                    EndMode3D();

                    // Прицел
                    int cx = GetScreenWidth()  / 2;
                    int cy = GetScreenHeight() / 2;
                    DrawLine(cx - 10, cy, cx + 10, cy, WHITE);
                    DrawLine(cx, cy - 10, cx, cy + 10, WHITE);

                    // HUD
                    DrawFPS(10, 10);
                    DrawText(TextFormat("Pos: %.1f %.1f %.1f", hero.pos.x, hero.pos.y, hero.pos.z),
                             10, 34, 20, WHITE);

                    EndDrawing();
    }

    CloseWindow();
    return 0;
}
