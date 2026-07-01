#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <unordered_map>
#include <cmath>
#include <vector>
#include <cstring>

// Константы мыши
const float MOUSE_SENSE = 0.003f;
const float MOUSE_UP_LIMIT = 1.5f;
const float MOUSE_DOWN_LIMIT = -1.5f;

// Константы персонажа
const float HERO_SPEED = 7.0f;
const float HERO_EYE = 1.6f;
const float HERO_JUMP = 7.0f;

// Константы мира
const int WORLD_SIZE = 1000;
const int CHUNK_SIZE = 32;
const float STEP = 0.05f;
const float MAX_DIST = 8.0f;
const int DRAW_DISTANCE = 3; // Дальность прорисовки
// Формула (2xDRAW_DISTANCE +1)x2^3(Потому что 3 измерения координат) =
// =(2x1 +1)^3 = 9 чанков вокруг игрока



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Структура мэша текстуры
struct UVRect {
    float u0, v0; // левый верхний угол тайла в атласе
    float u1, v1; // правый нижний угол
};
UVRect uvTop    = { 0.00f, 0.0f,  0.25f, 1.0f }; // тайл 0
UVRect uvSide   = { 0.25f, 0.0f,  0.50f, 1.0f }; // тайл 1
UVRect uvBottom = { 0.50f, 0.0f,  0.75f, 1.0f }; // тайл 2

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
    Texture2D atlas;
    UVRect top;
    UVRect side;
    UVRect bottom;
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



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



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



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class Chunk
{
public:
    Block blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    Mesh mesh = {};
    Model model = {};
    bool meshDirty = true;

    void Generate() {
        for (int x = 0; x < CHUNK_SIZE; x++) for (int y = 0; y < CHUNK_SIZE; y++) for (int z = 0; z < CHUNK_SIZE; z++) if (y != 0) blocks[x][y][z].type = BlockType::AIR; else blocks[x][y][z].type = BlockType::GRASS;
    }

    bool IsSolid(int x, int y, int z) {
        if (x < 0 || x >= CHUNK_SIZE ||
            y < 0 || y >= CHUNK_SIZE ||
            z < 0 || z >= CHUNK_SIZE)
            return false; // за границей чанка — считаем воздухом

            return blocks[x][y][z].type != BlockType::AIR;
    }

    void AddQuad(std::vector<float>& verts, std::vector<float>& uvs,
                 Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                 UVRect uv)
    {
        // Треугольник 1: A, B, C
        verts.push_back(a.x); verts.push_back(a.y); verts.push_back(a.z);
        verts.push_back(b.x); verts.push_back(b.y); verts.push_back(b.z);
        verts.push_back(c.x); verts.push_back(c.y); verts.push_back(c.z);

        // Треугольник 2: A, C, D
        verts.push_back(a.x); verts.push_back(a.y); verts.push_back(a.z);
        verts.push_back(c.x); verts.push_back(c.y); verts.push_back(c.z);
        verts.push_back(d.x); verts.push_back(d.y); verts.push_back(d.z);

        // UV треугольник 1
        uvs.push_back(uv.u0); uvs.push_back(uv.v1); // A — левый нижний
        uvs.push_back(uv.u1); uvs.push_back(uv.v1); // B — правый нижний
        uvs.push_back(uv.u1); uvs.push_back(uv.v0); // C — правый верхний

        // UV треугольник 2
        uvs.push_back(uv.u0); uvs.push_back(uv.v1); // A — левый нижний
        uvs.push_back(uv.u1); uvs.push_back(uv.v0); // C — правый верхний
        uvs.push_back(uv.u0); uvs.push_back(uv.v0); // D — левый верхний
    }

    void BuildMesh(std::unordered_map<BlockType, BlockTextures>& textures) {
        if (model.meshCount > 0) {
            UnloadModel(model);
            mesh = {};
            model = {};
        }

        std::vector<float> vertices;
        std::vector<float> texcoords;

        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int y = 0; y < CHUNK_SIZE; y++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    Block& block = blocks[x][y][z];
                    if (block.type == BlockType::AIR) continue;

                    BlockTextures& tex = textures[block.type];
                    float wx = (float)x;
                    float wy = (float)y;
                    float wz = (float)z;

                    if (!IsSolid(x, y+1, z))
                        AddQuad(vertices, texcoords,
                                {wx, wy+1, wz+1}, {wx+1, wy+1, wz+1},
                                {wx+1, wy+1, wz}, {wx, wy+1, wz}, tex.top);

                    if (!IsSolid(x, y-1, z))
                        AddQuad(vertices, texcoords,
                            {wx, wy, wz},   {wx+1, wy, wz},
                            {wx+1, wy, wz+1}, {wx, wy, wz+1}, tex.bottom);

                    if (!IsSolid(x, y, z+1))
                        AddQuad(vertices, texcoords,
                            {wx, wy, wz+1},   {wx+1, wy, wz+1},
                            {wx+1, wy+1, wz+1}, {wx, wy+1, wz+1}, tex.side);

                    if (!IsSolid(x, y, z-1))
                        AddQuad(vertices, texcoords,
                            {wx+1, wy, wz}, {wx, wy, wz},
                            {wx, wy+1, wz}, {wx+1, wy+1, wz}, tex.side);

                    if (!IsSolid(x+1, y, z))
                        AddQuad(vertices, texcoords,
                            {wx+1, wy, wz+1}, {wx+1, wy, wz},
                            {wx+1, wy+1, wz}, {wx+1, wy+1, wz+1}, tex.side);

                    if (!IsSolid(x-1, y, z))
                        AddQuad(vertices, texcoords,
                            {wx, wy, wz},   {wx, wy, wz+1},
                            {wx, wy+1, wz+1}, {wx, wy+1, wz}, tex.side);
                }

                if (vertices.empty()) return;

        mesh.vertexCount   = vertices.size() / 3;
        mesh.triangleCount = mesh.vertexCount / 3;

        mesh.vertices  = (float*)MemAlloc(vertices.size()  * sizeof(float));
        mesh.texcoords = (float*)MemAlloc(texcoords.size() * sizeof(float));

        memcpy(mesh.vertices,  vertices.data(),  vertices.size()  * sizeof(float));
        memcpy(mesh.texcoords, texcoords.data(), texcoords.size() * sizeof(float));

        Texture2D atlas = textures.begin()->second.atlas;

        UploadMesh(&mesh, false);
        model = LoadModelFromMesh(mesh);

        // Привязываем атлас к материалу модели
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = atlas;

        meshDirty = false;
    }
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class World
{
public:
    std::unordered_map<ChunkCoord, Chunk> chunks;
    std::unordered_map<BlockType, BlockTextures> textures;

    Chunk& GetChunk(int cx, int cy, int cz) {
        ChunkCoord key = {cx, cy, cz};

        auto it = chunks.find(key);
        if (it != chunks.end()) {
            return it->second;
        }

        Chunk& newChunk = chunks[key];
        newChunk.Generate();
        return newChunk;
    }

    void UpdateLoadedChunks(int playerChunkX, int playerChunkZ) {
        // 1. ВЫГРУЗКА: Удаляем чанки, которые вышли за пределы дальности прорисовки
        for (auto it = chunks.begin(); it != chunks.end(); ) {
            int cx = it->first.x;
            int cz = it->first.z;

            // Проверяем дистанцию от чанка до игрока
            if (std::abs(cx - playerChunkX) > DRAW_DISTANCE ||
                std::abs(cz - playerChunkZ) > DRAW_DISTANCE) {

                // Безопасно удаляем элемент и получаем итератор на следующий
                it = chunks.erase(it);
                } else {
                    ++it; // Чанк близко, просто идем к следующему
                }
        }

        // 2. ЗАГРУЗКА: Генерируем новые чанки вокруг игрока
        for (int dx = -DRAW_DISTANCE; dx <= DRAW_DISTANCE; dx++) {
            for (int dz = -DRAW_DISTANCE; dz <= DRAW_DISTANCE; dz++) {
                GetChunk(playerChunkX + dx, 0, playerChunkZ + dz);
            }
        }
    }

    void Draw() {
        for (auto& pair : chunks) {
            Chunk& chunk = pair.second;
            ChunkCoord coord = pair.first;

            if (chunk.meshDirty) chunk.BuildMesh(textures);

            if (chunk.model.meshCount > 0) {
                Vector3 chunkWorldPos = {
                    (float)(coord.x * CHUNK_SIZE),
                    (float)(coord.y * CHUNK_SIZE),
                    (float)(coord.z * CHUNK_SIZE)
                };
                DrawModel(chunk.model, chunkWorldPos, 1.0f, WHITE);
            }
        }
    }

    void DrawSelection(int hx, int hy, int hz) {
        // Подсветка выделенного блока
        DrawCubeWires(
            { hx + 0.5f, hy + 0.5f, hz + 0.5f },
            1.01f, 1.01f, 1.01f, WHITE
        );
    }

    bool IsBlockSolid(int wx, int wy, int wz) {
        // Отрицательные координаты обрабатываем правильно через floor
        int cx = (int)floorf((float)wx / CHUNK_SIZE);
        int cy = (int)floorf((float)wy / CHUNK_SIZE);
        int cz = (int)floorf((float)wz / CHUNK_SIZE);

        ChunkCoord key = {cx, cy, cz};
        auto it = chunks.find(key);
        if (it == chunks.end()) return false; // чанк не загружен — считаем воздухом

        // Локальные координаты внутри чанка
        int lx = wx - cx * CHUNK_SIZE;
        int ly = wy - cy * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;

        return it->second.blocks[lx][ly][lz].type != BlockType::AIR;
    }

    void BreakBlock(int wx, int wy, int wz) {
        int cx = (int)floorf((float)wx / CHUNK_SIZE);
        int cy = (int)floorf((float)wy / CHUNK_SIZE);
        int cz = (int)floorf((float)wz / CHUNK_SIZE);

        ChunkCoord key = {cx, cy, cz};
        auto it = chunks.find(key);
        if (it == chunks.end()) return;

        int lx = wx - cx * CHUNK_SIZE;
        int ly = wy - cy * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;

        it->second.blocks[lx][ly][lz].type = BlockType::AIR;
        it->second.meshDirty = true;
    }

    void PlaceBlock(int wx, int wy, int wz, BlockType type) {
        int cx = (int)floorf((float)wx / CHUNK_SIZE);
        int cy = (int)floorf((float)wy / CHUNK_SIZE);
        int cz = (int)floorf((float)wz / CHUNK_SIZE);

        ChunkCoord key = {cx, cy, cz};
        auto it = chunks.find(key);
        if (it == chunks.end()) return;

        int lx = wx - cx * CHUNK_SIZE;
        int ly = wy - cy * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;

        it->second.blocks[lx][ly][lz].type = type;
        it->second.meshDirty = true;
    }
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class Hero
{
public:
    Vector3 pos;
    float yaw   = 0.0f;
    float pitch = 0.0f;
    Vector3 velocity = {0, 0, 0}; // текущая скорость (особенно важна Y для гравитации)
    float speed = HERO_SPEED;
    bool onGround = false;
    const float HERO_WIDTH  = 0.6f; // ширина и глубина
    const float HERO_HEIGHT = 1.8f; // высота
    const float HERO_EYE    = 1.6f; // камера на уровне глаз от ног
    BlockType hotbar[9] = {
        BlockType::GRASS,
        BlockType::DIRT,
        BlockType::AIR,  // пусто
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
    };
    int selectedSlot = 0;

    Vector3 GetForward() {
        return { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
    }

    Vector3 GetRight() {
        return { sinf(yaw - PI/2), 0.0f, cosf(yaw - PI/2) };
    }

    bool CollidesWithWorld(Vector3 checkPos, World& world) {
        int x0 = (int)floorf(checkPos.x - HERO_WIDTH/2);
        int x1 = (int)floorf(checkPos.x + HERO_WIDTH/2);
        int y0 = (int)floorf(checkPos.y);
        int y1 = (int)floorf(checkPos.y + HERO_HEIGHT);
        int z0 = (int)floorf(checkPos.z - HERO_WIDTH/2);
        int z1 = (int)floorf(checkPos.z + HERO_WIDTH/2);

        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++)
                for (int z = z0; z <= z1; z++)
                    if (world.IsBlockSolid(x, y, z))
                        return true;

        return false;
    }

    void Rotate(Vector2 mouse) {
        yaw   -= mouse.x * MOUSE_SENSE;
        pitch -= mouse.y * MOUSE_SENSE;

        if (pitch >  MOUSE_UP_LIMIT)   pitch =  MOUSE_UP_LIMIT;
        if (pitch <  MOUSE_DOWN_LIMIT)  pitch =  MOUSE_DOWN_LIMIT;
    }

    void Update(Vector3 forward, Vector3 right, float dt, World& world) {
        Vector3 flatForward = Vector3Normalize({ forward.x, 0.0f, forward.z });

        if (onGround) {
            // На земле — клавиши напрямую задают горизонтальную скорость
            velocity.x = 0;
            velocity.z = 0;
            if (IsKeyDown(KEY_W)) { velocity.x += flatForward.x * speed; velocity.z += flatForward.z * speed; }
            if (IsKeyDown(KEY_S)) { velocity.x -= flatForward.x * speed; velocity.z -= flatForward.z * speed; }
            if (IsKeyDown(KEY_A)) { velocity.x -= right.x * speed; velocity.z -= right.z * speed; }
            if (IsKeyDown(KEY_D)) { velocity.x += right.x * speed; velocity.z += right.z * speed; }
        } else {
            // В воздухе — клавиши слегка корректируют, инерция сохраняется
            float airControl = 15.0f;
            if (IsKeyDown(KEY_W)) { velocity.x += flatForward.x * airControl * dt; velocity.z += flatForward.z * airControl * dt; }
            if (IsKeyDown(KEY_S)) { velocity.x -= flatForward.x * airControl * dt; velocity.z -= flatForward.z * airControl * dt; }
            if (IsKeyDown(KEY_A)) { velocity.x -= right.x * airControl * dt; velocity.z -= right.z * airControl * dt; }
            if (IsKeyDown(KEY_D)) { velocity.x += right.x * airControl * dt; velocity.z += right.z * airControl * dt; }

            // Ограничение горизонтальной скорости в воздухе
            float horizSpeed = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
            if (horizSpeed > speed) {
                float scale = speed / horizSpeed;
                velocity.x *= scale;
                velocity.z *= scale;
            }
        }

        // Гравитация
        velocity.y -= 20.0f * dt;

        // Применяем скорость по осям отдельно
        Vector3 newPos = pos;

        newPos.x = pos.x + velocity.x * dt;
        if (!CollidesWithWorld({newPos.x, pos.y, pos.z}, world)) pos.x = newPos.x;
        else velocity.x = 0;

        newPos.z = pos.z + velocity.z * dt;
        if (!CollidesWithWorld({pos.x, pos.y, newPos.z}, world)) pos.z = newPos.z;
        else velocity.z = 0;

        newPos.y = pos.y + velocity.y * dt;
        if (!CollidesWithWorld({pos.x, newPos.y, pos.z}, world)) {
            pos.y = newPos.y;
            onGround = false;
        } else {
            if (velocity.y < 0) onGround = true;
            velocity.y = 0;
        }

        // Прыжок
        if (IsKeyDown(KEY_SPACE) && onGround) {
            velocity.y = 7.0f;
            onGround = false;
        }

        // Респавн в случае падения
        if (pos.y < -100) pos.y = 4, pos.x+=2;
    }

    void DrawHotbar() {
        const int SLOT_SIZE = 50;
        const int SLOT_GAP  = 4;
        const int SLOTS     = 9;

        int totalWidth = SLOTS * SLOT_SIZE + (SLOTS - 1) * SLOT_GAP;
        int startX = GetScreenWidth()  / 2 - totalWidth / 2;
        int startY = GetScreenHeight() - SLOT_SIZE - 10;

        for (int i = 0; i < SLOTS; i++) {
            int x = startX + i * (SLOT_SIZE + SLOT_GAP);

            // Фон ячейки
            DrawRectangle(x, startY, SLOT_SIZE, SLOT_SIZE, { 0, 0, 0, 120 });

            // Рамка — белая для активной, серая для остальных
            Color borderColor = (i == selectedSlot) ? WHITE : GRAY;
            DrawRectangleLines(x, startY, SLOT_SIZE, SLOT_SIZE, borderColor);

            // Название блока в ячейке (пока без иконок)
            if (hotbar[i] == BlockType::GRASS)
                DrawText("GRS", x + 5, startY + SLOT_SIZE/2 - 8, 16, GREEN);
            else if (hotbar[i] == BlockType::DIRT)
                DrawText("DRT", x + 5, startY + SLOT_SIZE/2 - 8, 16, BROWN);

            // Номер слота
            DrawText(TextFormat("%d", i + 1), x + 3, startY + 3, 14, LIGHTGRAY);
        }
    }

    void HUD() {
        DrawFPS(10, 10);
        DrawText(TextFormat("Pos: %.1f %.1f %.1f", pos.x, pos.y, pos.z),
                 10, 34, 20, WHITE);

        int cx = GetScreenWidth()  / 2;
        int cy = GetScreenHeight() / 2;
        DrawLine(cx - 10, cy, cx + 10, cy, WHITE);
        DrawLine(cx, cy - 10, cx, cy + 10, WHITE);

        DrawHotbar();
        UpdateHotbar();
    }

    void UpdateHotbar() {
        // Цифры 1-9
        for (int i = 0; i < 9; i++) {
            if (IsKeyPressed(KEY_ONE + i))
                selectedSlot = i;
        }

        // Колесо мыши
        float wheel = GetMouseWheelMove();
        if (wheel > 0) selectedSlot = (selectedSlot - 1 + 9) % 9;
        if (wheel < 0) selectedSlot = (selectedSlot + 1) % 9;
    }

    bool RayCast(const Camera3D& camera, World& world,
                 int& hx, int& hy, int& hz,
                 int& nx, int& ny, int& nz) {

        Ray ray = GetMouseRay(
            { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f },
                              camera
        );

        int prevX = -1, prevY = -1, prevZ = -1;

        for (float t = 0; t < MAX_DIST; t += STEP) {
            Vector3 point = Vector3Add(ray.position, Vector3Scale(ray.direction, t));

            int x = (int)floorf(point.x);
            int y = (int)floorf(point.y);
            int z = (int)floorf(point.z);

            if (world.IsBlockSolid(x, y, z)) {
                hx = x; hy = y; hz = z;
                nx = prevX; ny = prevY; nz = prevZ;
                return true;
            }

            prevX = x; prevY = y; prevZ = z;
        }
        return false;
    }
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



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
    Texture2D atlasGrass           = LoadTexture("textures/atlas_grass.png");

    BlockTextures grassTextures;
    grassTextures.atlas  = atlasGrass;
    grassTextures.top    = { 0.00f, 0.0f, 0.25f, 1.0f };
    grassTextures.side   = { 0.25f, 0.0f, 0.50f, 1.0f };
    grassTextures.bottom = { 0.50f, 0.0f, 0.75f, 1.0f };
    world.textures[BlockType::GRASS] = grassTextures;

    Texture2D atlasDirt = LoadTexture("textures/atlas_dirt.png");

    BlockTextures dirtTextures;
    dirtTextures.atlas  = atlasDirt;
    dirtTextures.top    = { 0.00f, 0.0f, 0.25f, 1.0f };
    dirtTextures.side   = { 0.25f, 0.0f, 0.50f, 1.0f };
    dirtTextures.bottom = { 0.50f, 0.0f, 0.75f, 1.0f };
    world.textures[BlockType::DIRT] = dirtTextures;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Поворот и движение
        hero.Rotate(GetMouseDelta());
        Vector3 forward = hero.GetForward();
        Vector3 right   = hero.GetRight();
        hero.Update(forward, right, dt, world);

        // Камера следует за героем
        camera.position.x = hero.pos.x;
        camera.position.y = hero.pos.y + HERO_EYE;
        camera.position.z = hero.pos.z;

        camera.target = Vector3Add(camera.position, hero.GetForward());

        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 75.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        // Отрисовка
        BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode3D(camera);

        // Цель взгляда
        int hx = 0, hy = 0, hz = 0;
        int nx = 0, ny = 0, nz = 0;

        int playerChunkX = (int)floorf(hero.pos.x / CHUNK_SIZE);
        int playerChunkZ = (int)floorf(hero.pos.z / CHUNK_SIZE);

        if (hero.RayCast(camera, world, hx, hy, hz, nx, ny, nz)) {
            world.DrawSelection(hx, hy, hz);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                world.BreakBlock(hx, hy, hz);
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
            if (hero.hotbar[hero.selectedSlot] != BlockType::AIR)
                world.PlaceBlock(nx, ny, nz, hero.hotbar[hero.selectedSlot]);

        // Блоки мира
        world.UpdateLoadedChunks(playerChunkX, playerChunkZ);
        world.Draw();

        EndMode3D();

        // HUD
        hero.HUD();

        if (IsKeyDown(KEY_ESCAPE)) break;

        EndDrawing();
    }

    UnloadTexture(atlasGrass);
    UnloadTexture(atlasDirt);

    CloseWindow();
    return 0;
}
