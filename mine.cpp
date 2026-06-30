#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <unordered_map>
#include <vector>

// Константы мыши
const float MOUSE_SENSE = 0.003f;
const float MOUSE_UP_LIMIT = 1.5f;
const float MOUSE_DOWN_LIMIT = -1.5f;

// Константы персонажа
const float HERO_SPEED = 10.0f;

// Константы мира
const int WORLD_SIZE = 1000;
const int CHUNK_SIZE = 16;
const float STEP = 0.05f;
const float MAX_DIST = 8.0f;
const int DRAW_DISTANCE = 1; // Дальность прорисовки
// Формула (2xDRAW_DISTANCE +1)x2^3(Потому что 3 измерения координат) =
// =(2x1 +1)^3 = 9 чанков вокруг игрока

struct ChunkCoord {
  int x, y, z;
};

bool operator==(const ChunkCoord& a, const ChunkCoord& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

namespace std {
    template<>
    struct hash<ChunkCoord> {
        size_t operator()(const ChunkCoord& c) const {
            size_t h1 = std::hash<int>()(c.x);
            size_t h2 = std::hash<int>()(c.y);
            size_t h3 = std::hash<int>()(c.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

enum class BlockType {
    AIR,
    GRASS,
    DIRT,
};

struct BlockTextures {
    Texture2D top;
    Texture2D side;
    Texture2D bottom;
};

struct Vertex
{
    Vector3 position;
    Vector2 uv;
};

void DrawTexturedQuad(Texture2D texture, Vector3 v1, Vector3 v2, Vector3 v3, Vector3 v4) {
    rlSetTexture(texture.id);

    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);

    rlTexCoord2f(0.0f, 1.0f);
    rlVertex3f(v1.x, v1.y, v1.z);

    rlTexCoord2f(1.0f, 1.0f);
    rlVertex3f(v2.x, v2.y, v2.z);

    rlTexCoord2f(1.0f, 0.0f);
    rlVertex3f(v3.x, v3.y, v3.z);

    rlTexCoord2f(0.0f, 0.0f);
    rlVertex3f(v4.x, v4.y, v4.z);
    rlEnd();

    rlSetTexture(0);
}

class Block
{
public:
    BlockType type = BlockType::AIR;

    static void Draw(Vector3 position, Texture2D topTex, Texture2D sideTex, Texture2D bottomTex) {
        float x = position.x;
        float y = position.y;
        float z = position.z;

        // Верх
        DrawTexturedQuad(topTex,
                         {x,     y+1, z+1},
                         {x+1,   y+1, z+1},
                         {x+1,   y+1, z},
                         {x,     y+1, z}
        );

        // Низ
        DrawTexturedQuad(bottomTex,
                         {x,     y, z},
                         {x+1,   y, z},
                         {x+1,   y, z+1},
                         {x,     y, z+1}
        );

        // Зад (Z+)
        DrawTexturedQuad(sideTex,
                         {x,   y,   z+1},
                         {x+1, y,   z+1},
                         {x+1, y+1, z+1},
                         {x,   y+1, z+1}
        );

        // Перед (Z)
        DrawTexturedQuad(sideTex,
                         {x+1, y,   z},
                         {x,   y,   z},
                         {x,   y+1, z},
                         {x+1, y+1, z}
        );

        // Правая (X+)
        DrawTexturedQuad(sideTex,
                         {x+1, y,   z+1},
                         {x+1, y,   z},
                         {x+1, y+1, z},
                         {x+1, y+1, z+1}
        );

        // Левая (X)
        DrawTexturedQuad(sideTex,
                         {x, y,   z},
                         {x, y,   z+1},
                         {x, y+1, z+1},
                         {x, y+1, z}
        );

    }
};

class Chunk
{
public:
    Block blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];

    std::vector<Vertex> vertices;

    void Generate() {
        for (int x = 0; x < CHUNK_SIZE; x++) for (int y = 0; y < CHUNK_SIZE; y++) for (int z = 0; z < CHUNK_SIZE; z++) if (y != 0) blocks[x][y][z].type = BlockType::AIR; else blocks[x][y][z].type = BlockType::GRASS;
    }

    void BuildMesh();

    void AddVertex(float x, float y, float z, float u, float v)
    {
        Vertex vertex;

        vertex.position = {x, y, z};
        vertex.uv = {u, v};

        vertices.push_back(vertex);
    }

    void AddQuad(
        Vector3 v1,
        Vector3 v2,
        Vector3 v3,
        Vector3 v4)
    {
        AddVertex(v1.x,v1.y,v1.z,0,1);
        AddVertex(v2.x,v2.y,v2.z,1,1);
        AddVertex(v3.x,v3.y,v3.z,1,0);
        AddVertex(v4.x,v4.y,v4.z,0,0);
    }

    void DrawMesh(Texture2D texture)
    {
        rlSetTexture(texture.id);

        rlBegin(RL_QUADS);

        rlColor4ub(255,255,255,255);

        for (const Vertex& vertex : vertices)
        {
            rlTexCoord2f(vertex.uv.x, vertex.uv.y);
            rlVertex3f(
                vertex.position.x,
                vertex.position.y,
                vertex.position.z
            );
        }

        rlEnd();

        rlSetTexture(0);
    }
};

void Chunk::BuildMesh()
{
    vertices.clear();

    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < CHUNK_SIZE; y++)
            for (int z = 0; z < CHUNK_SIZE; z++)
            {
                if (blocks[x][y][z].type == BlockType::AIR)
                    continue;

                AddQuad(
                    {x,     y + 1.0f, z + 1.0f},
                    {x + 1.0f, y + 1.0f, z + 1.0f},
                    {x + 1.0f, y + 1.0f, z},
                    {x,     y + 1.0f, z}
                );
            }
}

class World
{
public:
    std::unordered_map<ChunkCoord, Chunk> chunks;

    Chunk& GetChunk(int cx, int cy, int cz) {
        ChunkCoord key = {cx, cy, cz};

        auto it = chunks.find(key);
        if (it != chunks.end()) {
            return it->second;
        }

        Chunk& newChunk = chunks[key];
        newChunk.Generate();
        newChunk.BuildMesh();
        return newChunk;
    }

    void UpdateLoadedChunks(int playerChunkX, int playerChunkZ) {
        for (int dx = -DRAW_DISTANCE; dx <= DRAW_DISTANCE; dx++) {
            for (int dz = -DRAW_DISTANCE; dz <= DRAW_DISTANCE; dz++) {
                GetChunk(playerChunkX + dx, 0, playerChunkZ + dz);
            }
        }
    }

    void Draw(    Texture2D grassTopTexture, Texture2D grassSideTexture, Texture2D dirtTexture) {
        for (auto& pair : chunks) pair.second.DrawMesh(grassTopTexture);
    }

    void DrawSelection(int hx, int hy, int hz) {
        // Подсветка выделенного блока
        DrawCubeWires(
            { hx + 0.5f, hy + 0.5f, hz + 0.5f },
            1.01f, 1.01f, 1.01f, WHITE
        );
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

    void HUD() {
        DrawFPS(10, 10);
        DrawText(TextFormat("Pos: %.1f %.1f %.1f", pos.x, pos.y, pos.z),
                 10, 34, 20, WHITE);

        int cx = GetScreenWidth()  / 2;
        int cy = GetScreenHeight() / 2;
        DrawLine(cx - 10, cy, cx + 10, cy, WHITE);
        DrawLine(cx, cy - 10, cx, cy + 10, WHITE);
    }

    bool RayCast(const Camera3D &camera, const World &world, int &hx, int &hy, int &hz) {
        return false; // TODO: переписать под чанки
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

    // Текстуры
    Texture2D grassTopTexture  = LoadTexture("textures/grasstop.png");
    Texture2D grassSideTexture = LoadTexture("textures/grass.png");
    Texture2D dirtTexture      = LoadTexture("textures/dirt.png");

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

        // Отрисовка
        BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode3D(camera);

        // Цель взгляда
        int hx = 0, hy = 0, hz = 0;
        if (hero.RayCast(camera, world, hx, hy, hz)) world.DrawSelection(hx, hy, hz);

        int playerChunkX = (int)floorf(hero.pos.x / CHUNK_SIZE);
        int playerChunkZ = (int)floorf(hero.pos.z / CHUNK_SIZE);

        // Блоки мира
        world.UpdateLoadedChunks(playerChunkX, playerChunkZ);
        world.Draw(grassTopTexture, grassSideTexture, dirtTexture);
        Block::Draw({0, 3, 0}, grassTopTexture, grassSideTexture, dirtTexture);

        EndMode3D();

        // HUD
        hero.HUD();

        EndDrawing();
    }

    UnloadTexture(grassTopTexture);
    UnloadTexture(grassSideTexture);
    UnloadTexture(dirtTexture);

    CloseWindow();
    return 0;
}
