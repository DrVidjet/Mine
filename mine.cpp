#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <unordered_map>
#include <cmath>
#include <vector>
#include <cstring>
#include <ctime>
#include <algorithm>

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
const float MAX_DIST = 8.0f;
const int DRAW_DISTANCE = 3; // Дальность прорисовки (по X/Z)
const int CHUNK_Y_MIN = -1; // Нижняя граница загруженных чанков по высоте
const int CHUNK_Y_MAX = 2;  // Верхняя граница (покрывает холмы, горы и ~50 блоков камня вниз)
// Формула (2xDRAW_DISTANCE +1)^2 x (CHUNK_Y_MAX-CHUNK_Y_MIN+1) чанков вокруг игрока

// Троттлинг подгрузки — сколько работы делаем максимум за один кадр, чтобы не было
// лаг-спайка при пересечении границы чанка (актуально особенно на слабых GPU)
const int MAX_CHUNK_GENERATIONS_PER_FRAME = 6;  // генерация рельефа — дёшево (только CPU)
const int MAX_MESH_BUILDS_PER_FRAME       = 2;  // постройка меша — дорого (аплоад в GPU)

// Константы генерации рельефа
const int   TERRAIN_BASE_HEIGHT     = 40;    // базовый уровень поверхности
const float HILL_FREQ               = 0.02f;
const float HILL_AMPLITUDE          = 6.0f;  // холмы
const float MOUNTAIN_FREQ           = 0.004f;
const float MOUNTAIN_AMPLITUDE      = 28.0f; // высота гор над базовым уровнем
const float MOUNTAIN_THRESHOLD      = 0.55f; // порог маски — выше него начинаются горы

// Константы лесов/полей и расстановки деревьев
const int   TREE_GRID           = 6;     // размер ячейки сетки расстановки (мировые блоки)
const float BIOME_FREQ          = 0.006f;
const float FOREST_THRESHOLD    = 0.5f;  // выше порога — лес, ниже — поле
const float FOREST_TREE_CHANCE  = 0.55f;
const float FIELD_TREE_CHANCE   = 0.04f; // редкие одиночные деревья на полях
const int   TREE_MOUNTAIN_LIMIT = 12;    // выше этого превышения над базовым уровнем деревья не растут

// Константы способности "молния" (Q)
const float MANA_REGEN_RATE          = 8.0f;  // маны в секунду
const float LIGHTNING_MANA_COST      = 25.0f;
const float LIGHTNING_DURATION       = 0.18f; // сколько секунд виден разряд
// Дальность молнии — по дальности прорисовки чанков, а не по короткой дистанции
// руки (MAX_DIST): иначе молния не могла бы достать дальше, чем можно сломать рукой
const float LIGHTNING_RANGE           = (float)(DRAW_DISTANCE * CHUNK_SIZE);
const float LIGHTNING_BREAK_RADIUS_SQ = 2.5f; // dx²+dy²+dz² <= это — кандидат в радиусе взрыва
const int   LIGHTNING_BREAK_CHANCE    = 65;   // % шанс, что конкретный блок в радиусе сломается
const int   LIGHTNING_PARTICLE_COUNT = 24;



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
    STONE,
    LOG,
    LEAVES,
};

enum class TreeSize {
    SMALL,
    MEDIUM,
    LARGE,
};

struct BlockTextures {
    Texture2D atlas;
    UVRect top;
    UVRect side;
    UVRect bottom;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Сид мира — выставляется один раз в main() перед первой генерацией (текущим временем),
// чтобы рельеф/леса были разными при каждом запуске, а не всегда одной и той же картой.
unsigned int gWorldSeed = 0;

// Детерминированный хэш-шум (value noise) для рельефа — без внешних зависимостей.
float Hash2D(int x, int z) {
    unsigned int h = (unsigned int)((x + (int)gWorldSeed) * 374761393 + (z + (int)gWorldSeed * 7) * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF; // [0,1)
}

float SmoothNoise2D(float x, float z) {
    int x0 = (int)floorf(x), z0 = (int)floorf(z);
    int x1 = x0 + 1, z1 = z0 + 1;
    float tx = x - x0, tz = z - z0;
    // Сглаживание (smoothstep), чтобы интерполяция не выглядела "блочной"
    float sx = tx * tx * (3 - 2 * tx);
    float sz = tz * tz * (3 - 2 * tz);

    float n00 = Hash2D(x0, z0);
    float n10 = Hash2D(x1, z0);
    float n01 = Hash2D(x0, z1);
    float n11 = Hash2D(x1, z1);

    float nx0 = n00 + (n10 - n00) * sx;
    float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sz; // [0,1)
}

// fBm — несколько октав шума, результат примерно в [0,1]
float FractalNoise2D(float x, float z, int octaves, float persistence) {
    float total = 0.0f, amplitude = 1.0f, freq = 1.0f, maxValue = 0.0f;
    for (int i = 0; i < octaves; i++) {
        total += SmoothNoise2D(x * freq, z * freq) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        freq *= 2.0f;
    }
    return total / maxValue;
}

// Высота поверхности в мировых координатах: холмы + локализованные горы
int TerrainHeight(int wx, int wz) {
    float hills = FractalNoise2D(wx * HILL_FREQ, wz * HILL_FREQ, 4, 0.5f);
    float mountainMask = FractalNoise2D(wx * MOUNTAIN_FREQ, wz * MOUNTAIN_FREQ, 3, 0.5f);

    float mountainFactor = 0.0f;
    if (mountainMask > MOUNTAIN_THRESHOLD) {
        float t = (mountainMask - MOUNTAIN_THRESHOLD) / (1.0f - MOUNTAIN_THRESHOLD);
        mountainFactor = t * t * (3 - 2 * t); // smoothstep — плавный переход к горам
    }

    float height = TERRAIN_BASE_HEIGHT
                 + (hills - 0.5f) * 2.0f * HILL_AMPLITUDE
                 + mountainFactor * MOUNTAIN_AMPLITUDE;
    return (int)floorf(height);
}

// Толщина слоя земли под травой — неравномерная, 3..5 блоков
int DirtDepth(int wx, int wz) {
    float n = FractalNoise2D(wx * 0.1f + 1000.0f, wz * 0.1f + 1000.0f, 2, 0.5f);
    return 3 + (int)floorf(n * 3.0f);
}

// Толщина слоя камня под землёй — неравномерная, около 50 блоков (45..54)
int StoneDepth(int wx, int wz) {
    float n = FractalNoise2D(wx * 0.03f + 2000.0f, wz * 0.03f + 2000.0f, 2, 0.5f);
    return 45 + (int)floorf(n * 10.0f);
}

// Является ли точка лесом (иначе — поле): крупные пятна биома через низкочастотный шум
bool IsForest(int wx, int wz) {
    float biome = FractalNoise2D(wx * BIOME_FREQ, wz * BIOME_FREQ, 3, 0.5f);
    return biome > FOREST_THRESHOLD;
}

struct TreeParams {
    int trunkHeight;
    int canopyRadiusXZ;
    int canopyRadiusY;
    int canopyOverlap; // насколько крона "садится" на верхушку ствола
};

TreeParams GetTreeParams(TreeSize size) {
    switch (size) {
        case TreeSize::SMALL:  return { 3, 2, 2, 1 };
        case TreeSize::MEDIUM: return { 5, 3, 2, 1 };
        case TreeSize::LARGE:  return { 8, 4, 3, 2 };
    }
    return { 3, 2, 2, 1 };
}

// Есть ли дерево в ячейке сетки расстановки, где именно (джиттер) и какого размера
bool TryGetTreeInCell(int cellX, int cellZ, int& outWx, int& outWz, TreeSize& outSize) {
    int wxCenter = cellX * TREE_GRID;
    int wzCenter = cellZ * TREE_GRID;

    bool forest = IsForest(wxCenter, wzCenter);
    float chance = forest ? FOREST_TREE_CHANCE : FIELD_TREE_CHANCE;

    float roll = Hash2D(cellX * 92821 + 17, cellZ * 68917 + 31);
    if (roll > chance) return false;

    float jx = Hash2D(cellX * 12451 + 5, cellZ * 91423 + 7);
    float jz = Hash2D(cellX * 55441 + 9, cellZ * 33911 + 3);
    outWx = wxCenter + (int)(jx * TREE_GRID);
    outWz = wzCenter + (int)(jz * TREE_GRID);

    float sizeRoll = Hash2D(cellX * 20483 + 13, cellZ * 40961 + 21);
    if (sizeRoll < 0.55f)      outSize = TreeSize::SMALL;
    else if (sizeRoll < 0.88f) outSize = TreeSize::MEDIUM;
    else                        outSize = TreeSize::LARGE;

    return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class Block
{
public:
    BlockType type = BlockType::AIR;
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class World; // вперёд объявлен — нужен для World-aware culling граней в BuildMesh

class Chunk
{
public:
    Block blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    std::unordered_map<BlockType, Mesh>  meshes;
    std::unordered_map<BlockType, Model> models;
    bool meshDirty = true;
    bool treesPlaced = false; // используется как метка для (cx,0,cz) — деревья на колонке уже расставлены
    bool hasAnyBlock = false; // false — чанк полностью пуст, меш строить не нужно

    Chunk() = default;
    // Модели владеют GPU-ресурсами (VAO/VBO) напрямую по id — копирование Chunk
    // привело бы к двойному UnloadModel одних и тех же ресурсов при уничтожении.
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    ~Chunk() {
        // Без этого удаление чанка из World::chunks (выгрузка по дальности прорисовки)
        // никогда не освобождало VAO/VBO меша — утечка видеопамяти при каждом перемещении игрока
        for (auto& pair : models) UnloadModel(pair.second);
    }

    void Generate(int chunkX, int chunkY, int chunkZ) {
        hasAnyBlock = false;
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                int wx = chunkX * CHUNK_SIZE + x;
                int wz = chunkZ * CHUNK_SIZE + z;

                int surface     = TerrainHeight(wx, wz);
                int dirtDepth   = DirtDepth(wx, wz);
                int stoneDepth  = StoneDepth(wx, wz);
                int stoneBottom = surface - dirtDepth - stoneDepth;

                for (int y = 0; y < CHUNK_SIZE; y++) {
                    int wy = chunkY * CHUNK_SIZE + y;

                    BlockType type;
                    if (wy > surface)                type = BlockType::AIR;
                    else if (wy == surface)           type = BlockType::GRASS;
                    else if (wy > surface - dirtDepth) type = BlockType::DIRT;
                    else if (wy > stoneBottom)         type = BlockType::STONE;
                    else                               type = BlockType::AIR;

                    blocks[x][y][z].type = type;
                    if (type != BlockType::AIR) hasAnyBlock = true;
                }
            }
        }
        if (!hasAnyBlock) meshDirty = false; // пустой чанк — сразу помечаем как не требующий меша
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

    // Реализация — после класса World (нужен World::IsBlockSolid для culling граней
    // на стыках чанков; см. определение Chunk::BuildMesh ниже).
    void BuildMesh(World& world, int chunkX, int chunkY, int chunkZ);

    void DrawMeshes(Vector3 chunkWorldPos) {
        for (auto& pair : models) {
            DrawModel(pair.second, chunkWorldPos, 1.0f, WHITE);
        }
    }
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



// Плоскость усечения вида Ax+By+Cz+D=0, нормаль направлена внутрь усечённой пирамиды
struct FrustumPlane {
    Vector3 normal;
    float d;
};

// Извлечение 6 плоскостей усечения из объединённой матрицы вида*проекции (метод Gribb/Hartmann)
void ExtractFrustumPlanes(Matrix vp, FrustumPlane out[6]) {
    // Матрица raylib хранится по столбцам: строка i — это (m_i, m_{i+4}, m_{i+8}, m_{i+12})
    float row0[4] = { vp.m0, vp.m4, vp.m8,  vp.m12 };
    float row1[4] = { vp.m1, vp.m5, vp.m9,  vp.m13 };
    float row2[4] = { vp.m2, vp.m6, vp.m10, vp.m14 };
    float row3[4] = { vp.m3, vp.m7, vp.m11, vp.m15 };

    auto makePlane = [](float sign, const float* rowA, const float* row3_) {
        FrustumPlane p;
        p.normal = { row3_[0] + sign * rowA[0], row3_[1] + sign * rowA[1], row3_[2] + sign * rowA[2] };
        p.d = row3_[3] + sign * rowA[3];
        float len = sqrtf(p.normal.x * p.normal.x + p.normal.y * p.normal.y + p.normal.z * p.normal.z);
        if (len > 0.00001f) { p.normal.x /= len; p.normal.y /= len; p.normal.z /= len; p.d /= len; }
        return p;
    };

    out[0] = makePlane( 1.0f, row0, row3); // left
    out[1] = makePlane(-1.0f, row0, row3); // right
    out[2] = makePlane( 1.0f, row1, row3); // bottom
    out[3] = makePlane(-1.0f, row1, row3); // top
    out[4] = makePlane( 1.0f, row2, row3); // near
    out[5] = makePlane(-1.0f, row2, row3); // far
}

// Консервативная проверка: true, если AABB целиком лежит за какой-то из плоскостей
// (значит гарантированно не виден; ложноположительных "не виден" не бывает)
bool AabbOutsideFrustum(const FrustumPlane planes[6], Vector3 minB, Vector3 maxB) {
    for (int i = 0; i < 6; i++) {
        Vector3 p = {
            (planes[i].normal.x >= 0) ? maxB.x : minB.x,
            (planes[i].normal.y >= 0) ? maxB.y : minB.y,
            (planes[i].normal.z >= 0) ? maxB.z : minB.z,
        };
        float dist = planes[i].normal.x * p.x + planes[i].normal.y * p.y + planes[i].normal.z * p.z + planes[i].d;
        if (dist < 0.0f) return true;
    }
    return false;
}


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
        newChunk.Generate(cx, cy, cz);
        return newChunk;
    }

    // throttled=false — снять лимит на кадр (используется один раз при старте, чтобы
    // площадка вокруг спавна была полностью готова сразу, без "дырок" в мире).
    // throttled=true (по умолчанию, каждый кадр в игровом цикле) — генерируем не больше
    // MAX_CHUNK_GENERATIONS_PER_FRAME НОВЫХ чанков за кадр, остальное — в следующих кадрах.
    // Без этого при пересечении границы чанка одним кадром генерировались/строились
    // меши для десятков чанков разом — отсюда и лаг подгрузки, особенно на слабых GPU.
    void UpdateLoadedChunks(int playerChunkX, int playerChunkZ, bool throttled = true) {
        // 1. ВЫГРУЗКА: Удаляем чанки, которые вышли за пределы дальности прорисовки
        for (auto it = chunks.begin(); it != chunks.end(); ) {
            int cx = it->first.x;
            int cz = it->first.z;

            // Проверяем дистанцию от чанка до игрока (по вертикали грузим фиксированный диапазон)
            if (std::abs(cx - playerChunkX) > DRAW_DISTANCE ||
                std::abs(cz - playerChunkZ) > DRAW_DISTANCE) {

                // Безопасно удаляем элемент и получаем итератор на следующий
                it = chunks.erase(it);
                } else {
                    ++it; // Чанк близко, просто идем к следующему
                }
        }

        // 2. ЗАГРУЗКА РЕЛЬЕФА: генерируем рельеф с ограничением на количество новых
        // чанков за кадр — уже существующие чанки не расходуют бюджет
        int genBudget = throttled ? MAX_CHUNK_GENERATIONS_PER_FRAME : (1 << 30);
        for (int dx = -DRAW_DISTANCE; dx <= DRAW_DISTANCE; dx++) {
            for (int dz = -DRAW_DISTANCE; dz <= DRAW_DISTANCE; dz++) {
                for (int cy = CHUNK_Y_MIN; cy <= CHUNK_Y_MAX; cy++) {
                    int cx = playerChunkX + dx, cz = playerChunkZ + dz;
                    if (chunks.find(ChunkCoord{cx, cy, cz}) != chunks.end()) continue;
                    if (genBudget <= 0) continue; // отложим до следующего кадра
                    GetChunk(cx, cy, cz);
                    genBudget--;
                }
            }
        }

        // 3. ДЕКОРАЦИИ (деревья): отдельным проходом, когда весь рельеф уже на месте —
        // так дерево у границы чанка может "залезть" в уже сгенерированного соседа.
        // Декорируем колонку, только если ВСЕ её вертикальные слои уже сгенерированы
        // (при троттлинге они могут доехать не в этом кадре, а в одном из следующих).
        for (int dx = -DRAW_DISTANCE; dx <= DRAW_DISTANCE; dx++) {
            for (int dz = -DRAW_DISTANCE; dz <= DRAW_DISTANCE; dz++) {
                int cx = playerChunkX + dx;
                int cz = playerChunkZ + dz;

                bool columnReady = true;
                for (int cy = CHUNK_Y_MIN; cy <= CHUNK_Y_MAX; cy++) {
                    if (chunks.find(ChunkCoord{cx, cy, cz}) == chunks.end()) { columnReady = false; break; }
                }
                if (!columnReady) continue;

                Chunk& anchor = chunks.at(ChunkCoord{cx, 0, cz}); // (cy=0) как метка "колонка декорирована"
                if (!anchor.treesPlaced) {
                    DecorateChunkColumn(cx, cz);
                    anchor.treesPlaced = true;
                }
            }
        }
    }

    void PlaceTree(int tx, int tz, TreeSize size) {
        int surface = TerrainHeight(tx, tz);
        if (surface - TERRAIN_BASE_HEIGHT > TREE_MOUNTAIN_LIMIT) return; // на крутых горах не растут

        TreeParams p = GetTreeParams(size);
        int baseY = surface + 1;

        for (int i = 0; i < p.trunkHeight; i++)
            PlaceBlock(tx, baseY + i, tz, BlockType::LOG);

        int centerY = baseY + p.trunkHeight - p.canopyOverlap;
        int rXZ = p.canopyRadiusXZ;
        int rY  = p.canopyRadiusY;

        for (int dx = -rXZ; dx <= rXZ; dx++) {
            for (int dz = -rXZ; dz <= rXZ; dz++) {
                for (int dy = -rY; dy <= rY; dy++) {
                    float nx = (float)dx / rXZ;
                    float ny = (float)dy / rY;
                    float nz = (float)dz / rXZ;
                    if (nx * nx + ny * ny + nz * nz > 1.0f) continue; // скругляем крону эллипсоидом

                    int lx = tx + dx, ly = centerY + dy, lz = tz + dz;
                    if (!IsBlockSolid(lx, ly, lz)) // не перезатираем ствол/рельеф
                        PlaceBlock(lx, ly, lz, BlockType::LEAVES);
                }
            }
        }
    }

    void DecorateChunkColumn(int cx, int cz) {
        int wx0 = cx * CHUNK_SIZE, wx1 = wx0 + CHUNK_SIZE - 1;
        int wz0 = cz * CHUNK_SIZE, wz1 = wz0 + CHUNK_SIZE - 1;

        // Запас в ±1 ячейку — деревья из соседних ячеек могут джиттером попасть в этот чанк
        int cellX0 = (int)floorf((float)wx0 / TREE_GRID) - 1;
        int cellX1 = (int)floorf((float)wx1 / TREE_GRID) + 1;
        int cellZ0 = (int)floorf((float)wz0 / TREE_GRID) - 1;
        int cellZ1 = (int)floorf((float)wz1 / TREE_GRID) + 1;

        for (int gx = cellX0; gx <= cellX1; gx++) {
            for (int gz = cellZ0; gz <= cellZ1; gz++) {
                int tx, tz;
                TreeSize size;
                if (TryGetTreeInCell(gx, gz, tx, tz, size))
                    PlaceTree(tx, tz, size);
            }
        }
    }

    void Draw(const Camera3D& camera) {
        Matrix view = GetCameraMatrix(camera);
        float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
        Matrix proj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
        Matrix vp = MatrixMultiply(view, proj);

        FrustumPlane planes[6];
        ExtractFrustumPlanes(vp, planes);

        // Ограничиваем число ПЕРЕСТРОЕНИЙ меша за кадр (аплоад в GPU — самая дорогая часть
        // подгрузки). Чанк с устаревшим мешем просто рисуется как есть ещё один кадр —
        // это на порядок дешевле, чем лаг-спайк от постройки полутора десятков мешей разом.
        int meshBuildBudget = MAX_MESH_BUILDS_PER_FRAME;

        for (auto& pair : chunks) {
            Chunk& chunk = pair.second;
            if (!chunk.hasAnyBlock) continue; // пустой чанк — рисовать нечего

            ChunkCoord coord = pair.first;
            Vector3 chunkWorldPos = {
                (float)(coord.x * CHUNK_SIZE),
                (float)(coord.y * CHUNK_SIZE),
                (float)(coord.z * CHUNK_SIZE)
            };
            Vector3 chunkMax = Vector3AddValue(chunkWorldPos, (float)CHUNK_SIZE);

            if (AabbOutsideFrustum(planes, chunkWorldPos, chunkMax)) continue;

            if (chunk.meshDirty && meshBuildBudget > 0) {
                chunk.BuildMesh(*this, coord.x, coord.y, coord.z);
                meshBuildBudget--;
            }
            chunk.DrawMeshes(chunkWorldPos);
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

    // Правка блока на границе чанка (lx/ly/lz == 0 или CHUNK_SIZE-1) меняет то,
    // что должен рисовать СОСЕДНИЙ чанк (culling граней теперь смотрит через границу —
    // см. Chunk::BuildMesh), поэтому его тоже нужно пометить грязным, если он загружен.
    void MarkNeighborDirty(int cx, int cy, int cz, int lx, int ly, int lz) {
        if (lx == 0)               MarkChunkDirty(cx - 1, cy, cz);
        if (lx == CHUNK_SIZE - 1)  MarkChunkDirty(cx + 1, cy, cz);
        if (ly == 0)               MarkChunkDirty(cx, cy - 1, cz);
        if (ly == CHUNK_SIZE - 1)  MarkChunkDirty(cx, cy + 1, cz);
        if (lz == 0)               MarkChunkDirty(cx, cy, cz - 1);
        if (lz == CHUNK_SIZE - 1)  MarkChunkDirty(cx, cy, cz + 1);
    }

    void MarkChunkDirty(int cx, int cy, int cz) {
        auto it = chunks.find(ChunkCoord{cx, cy, cz});
        if (it != chunks.end()) it->second.meshDirty = true;
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
        MarkNeighborDirty(cx, cy, cz, lx, ly, lz);
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
        if (type != BlockType::AIR) it->second.hasAnyBlock = true;
        MarkNeighborDirty(cx, cy, cz, lx, ly, lz);
    }
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Определено здесь (а не внутри class Chunk), т.к. требует полного типа World
// для проверки соседних чанков через границу — см. комментарий у объявления.
void Chunk::BuildMesh(World& world, int chunkX, int chunkY, int chunkZ) {
    // Выгружаем старые меши
    for (auto& pair : models) UnloadModel(pair.second);
    meshes.clear();
    models.clear();

    if (!hasAnyBlock) { meshDirty = false; return; } // пустой чанк — строить нечего

    // За пределами локального чанка спрашиваем мир, а не считаем воздухом — иначе
    // на каждой границе чанка (X/Z и теперь Y-слоёв) рисуются лишние скрытые грани
    auto solidAt = [&](int x, int y, int z) -> bool {
        if (x >= 0 && x < CHUNK_SIZE && y >= 0 && y < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE)
            return IsSolid(x, y, z);
        int wx = chunkX * CHUNK_SIZE + x;
        int wy = chunkY * CHUNK_SIZE + y;
        int wz = chunkZ * CHUNK_SIZE + z;
        return world.IsBlockSolid(wx, wy, wz);
    };

    // Отдельный вектор вершин для каждого типа блока
    std::unordered_map<BlockType, std::vector<float>> vertMap;
    std::unordered_map<BlockType, std::vector<float>> uvMap;

    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < CHUNK_SIZE; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                Block& block = blocks[x][y][z];
                if (block.type == BlockType::AIR) continue;

                BlockTextures& tex = world.textures[block.type];
                float wx = (float)x;
                float wy = (float)y;
                float wz = (float)z;

                auto& verts = vertMap[block.type];
                auto& uvs   = uvMap[block.type];

                if (!solidAt(x, y + 1, z))
                    AddQuad(verts, uvs, {wx, wy+1, wz+1}, {wx+1, wy+1, wz+1},
                            {wx+1, wy+1, wz}, {wx, wy+1, wz}, tex.top);

                if (!solidAt(x, y - 1, z))
                    AddQuad(verts, uvs, {wx, wy, wz}, {wx+1, wy, wz},
                            {wx+1, wy, wz+1}, {wx, wy, wz+1}, tex.bottom);

                if (!solidAt(x, y, z + 1))
                    AddQuad(verts, uvs, {wx, wy, wz+1}, {wx+1, wy, wz+1},
                            {wx+1, wy+1, wz+1}, {wx, wy+1, wz+1}, tex.side);

                if (!solidAt(x, y, z - 1))
                    AddQuad(verts, uvs, {wx+1, wy, wz}, {wx, wy, wz},
                            {wx, wy+1, wz}, {wx+1, wy+1, wz}, tex.side);

                if (!solidAt(x + 1, y, z))
                    AddQuad(verts, uvs, {wx+1, wy, wz+1}, {wx+1, wy, wz},
                            {wx+1, wy+1, wz}, {wx+1, wy+1, wz+1}, tex.side);

                if (!solidAt(x - 1, y, z))
                    AddQuad(verts, uvs, {wx, wy, wz}, {wx, wy, wz+1},
                            {wx, wy+1, wz+1}, {wx, wy+1, wz}, tex.side);
            }

    // Для каждого типа блока собираем отдельный меш
    for (auto& pair : vertMap) {
        BlockType type = pair.first;
        auto& verts = pair.second;
        auto& uvs   = uvMap[type];

        if (verts.empty()) continue;

        Mesh m = {};
        m.vertexCount   = verts.size() / 3;
        m.triangleCount = m.vertexCount / 3;

        m.vertices  = (float*)MemAlloc(verts.size() * sizeof(float));
        m.texcoords = (float*)MemAlloc(uvs.size()   * sizeof(float));

        memcpy(m.vertices,  verts.data(), verts.size() * sizeof(float));
        memcpy(m.texcoords, uvs.data(),   uvs.size()   * sizeof(float));

        UploadMesh(&m, false);
        Model mdl = LoadModelFromMesh(m);
        mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = world.textures[type].atlas;

        meshes[type] = m;
        models[type] = mdl;
    }

    meshDirty = false;
}



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
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
        BlockType::AIR,
    };
    int selectedSlot = 0;

    float hp = 100.0f;
    float maxHp = 100.0f;
    float mana = 100.0f;
    float maxMana = 100.0f;

    Vector3 GetForward() {
        return { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
    }

    Vector3 GetRight() {
        return { sinf(yaw - PI/2), 0.0f, cosf(yaw - PI/2) };
    }

    Vector3 GetUp() {
        // Настоящий "верх" камеры с учётом pitch (не мировой (0,1,0)) — иначе руки
        // "отклеивались" бы от экрана при взгляде вверх/вниз
        return Vector3CrossProduct(GetRight(), GetForward());
    }

    Vector3 GetRightHandPos(Vector3 eyePos) {
        Vector3 p = eyePos;
        p = Vector3Add(p, Vector3Scale(GetForward(), 0.6f));
        p = Vector3Add(p, Vector3Scale(GetRight(), 0.35f));
        p = Vector3Add(p, Vector3Scale(GetUp(), -0.35f));
        return p;
    }

    Vector3 GetLeftHandPos(Vector3 eyePos) {
        Vector3 p = eyePos;
        p = Vector3Add(p, Vector3Scale(GetForward(), 0.6f));
        p = Vector3Add(p, Vector3Scale(GetRight(), -0.35f));
        p = Vector3Add(p, Vector3Scale(GetUp(), -0.35f));
        return p;
    }

    // DrawCube всегда рисует по мировым осям, без поворота — сама по себе позиция
    // "едет" за камерой, но кубик остаётся развёрнут как стоял. Поэтому кубик рисуем
    // в его локальном (0,0,0) внутри повёрнутой матрицы rlgl — сначала рысканье (yaw)
    // вокруг мировой Y, потом тангаж (pitch) вокруг получившейся локальной X;
    // тот же порядок вращений, которым построен GetForward().
    void DrawHandCube(Vector3 pos, Color skin) {
        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        rlRotatef(yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlRotatef(-pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
        DrawCube({ 0, 0, 0 }, 0.18f, 0.18f, 0.45f, skin);
        DrawCubeWires({ 0, 0, 0 }, 0.18f, 0.18f, 0.45f, Fade(BLACK, 0.4f));
        rlPopMatrix();
    }

    // Простые блочные руки от первого лица — под стиль игры, без отдельной модели
    void DrawHands(Vector3 eyePos) {
        Color skin = { 224, 172, 132, 255 };
        DrawHandCube(GetRightHandPos(eyePos), skin);
        DrawHandCube(GetLeftHandPos(eyePos), skin);
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

    // Пересекается ли блок (bx,by,bz) с текущим хитбоксом героя — используется, чтобы
    // не дать поставить блок прямо в себя (например, глядя вниз в прыжке): без этой
    // проверки игрок мог застрять внутри только что поставленного блока — обычное
    // столкновение (CollidesWithWorld) не умеет "вытолкнуть" уже проникшего игрока.
    bool WouldOverlapBlock(int bx, int by, int bz) {
        float hx0 = pos.x - HERO_WIDTH/2,  hx1 = pos.x + HERO_WIDTH/2;
        float hy0 = pos.y,                  hy1 = pos.y + HERO_HEIGHT;
        float hz0 = pos.z - HERO_WIDTH/2,  hz1 = pos.z + HERO_WIDTH/2;

        float bx0 = (float)bx, bx1 = bx0 + 1.0f;
        float by0 = (float)by, by1 = by0 + 1.0f;
        float bz0 = (float)bz, bz1 = bz0 + 1.0f;

        return (hx0 < bx1 && hx1 > bx0) &&
               (hy0 < by1 && hy1 > by0) &&
               (hz0 < bz1 && hz1 > bz0);
    }

    void Rotate(Vector2 mouse) {
        yaw   -= mouse.x * MOUSE_SENSE;
        pitch -= mouse.y * MOUSE_SENSE;

        if (pitch >  MOUSE_UP_LIMIT)   pitch =  MOUSE_UP_LIMIT;
        if (pitch <  MOUSE_DOWN_LIMIT)  pitch =  MOUSE_DOWN_LIMIT;
    }

    void Update(Vector3 forward, Vector3 right, float dt, World& world) {
        mana += MANA_REGEN_RATE * dt;
        if (mana > maxMana) mana = maxMana;

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
        if (pos.y < -100) pos.y = 40, pos.x+=2;
    }

    void DrawHotbar() {
        const int SLOT_SIZE = 50;
        const int SLOT_GAP  = 4;
        const int SLOTS     = 9;

        int totalWidth = SLOTS * SLOT_SIZE + (SLOTS - 1) * SLOT_GAP;
        // Сдвинут правее центра, чтобы не пересекаться со шкалами HP/маны внизу слева
        int startX = GetScreenWidth() * 2 / 3 - totalWidth / 2;
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
            else if (hotbar[i] == BlockType::STONE)
                DrawText("STN", x + 5, startY + SLOT_SIZE/2 - 8, 16, GRAY);
            else if (hotbar[i] == BlockType::LOG)
                DrawText("LOG", x + 5, startY + SLOT_SIZE/2 - 8, 16, BROWN);
            else if (hotbar[i] == BlockType::LEAVES)
                DrawText("LEF", x + 5, startY + SLOT_SIZE/2 - 8, 16, DARKGREEN);

            // Номер слота
            DrawText(TextFormat("%d", i + 1), x + 3, startY + 3, 14, LIGHTGRAY);
        }
    }

    // Полоса-"таблетка" с тенью, скруглением, двухцветным заполнением (для объёма)
    // и подписью со значением поверх — используется и для HP, и для маны.
    void DrawStatBar(int x, int y, int width, int height, float value, float maxValue,
                      Color fillColor, Color fillColorDark, const char* label) {
        float ratio = (maxValue > 0.0f) ? Clamp(value / maxValue, 0.0f, 1.0f) : 0.0f;
        float roundness = 0.6f;
        int segments = 8;

        // Тень под полосой — лёгкая глубина
        DrawRectangleRounded({ (float)x + 2, (float)y + 3, (float)width, (float)height }, roundness, segments, Fade(BLACK, 0.35f));
        // Подложка (пустая часть шкалы)
        DrawRectangleRounded({ (float)x, (float)y, (float)width, (float)height }, roundness, segments, { 24, 22, 28, 230 });

        // Заполнение: тёмная подложка-обводка + светлее сверху — создаёт объём
        float fillWidth = width * ratio;
        if (fillWidth > 2.0f) {
            DrawRectangleRounded({ (float)x, (float)y, fillWidth, (float)height }, roundness, segments, fillColorDark);
            float innerW = fillWidth - 4.0f;
            if (innerW > 0.0f)
                DrawRectangleRounded({ (float)x + 2, (float)y + 2, innerW, (float)height - 4 }, roundness, segments, fillColor);
        }

        // Рамка
        DrawRectangleRoundedLinesEx({ (float)x, (float)y, (float)width, (float)height }, roundness, segments, 2.0f, { 240, 240, 245, 220 });

        // Подпись слева, значение справа
        DrawText(label, x + 10, y + height / 2 - 8, 16, WHITE);
        const char* valueText = TextFormat("%d/%d", (int)value, (int)maxValue);
        int valueWidth = MeasureText(valueText, 14);
        DrawText(valueText, x + width - valueWidth - 10, y + height / 2 - 7, 14, WHITE);
    }

    void DrawStatusBars() {
        const int BAR_WIDTH  = 220;
        const int BAR_HEIGHT = 24;
        const int BAR_GAP    = 8;
        const int MARGIN     = 14;

        int hpY   = GetScreenHeight() - MARGIN - BAR_HEIGHT * 2 - BAR_GAP;
        int manaY = hpY + BAR_HEIGHT + BAR_GAP;

        DrawStatBar(MARGIN, hpY, BAR_WIDTH, BAR_HEIGHT, hp, maxHp,
                    { 235, 70, 70, 255 }, { 120, 20, 20, 255 }, "HP");
        DrawStatBar(MARGIN, manaY, BAR_WIDTH, BAR_HEIGHT, mana, maxMana,
                    { 90, 150, 255, 255 }, { 20, 50, 140, 255 }, "MP");
    }

    void HUD() {
        DrawFPS(10, 10);
        DrawText(TextFormat("Pos: %.1f %.1f %.1f", pos.x, pos.y, pos.z),
                 10, 34, 20, WHITE);

        int cx = GetScreenWidth()  / 2;
        int cy = GetScreenHeight() / 2;
        DrawLine(cx - 10, cy, cx + 10, cy, WHITE);
        DrawLine(cx, cy - 10, cx, cy + 10, WHITE);

        DrawStatusBars();
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

    // Воксельный DDA (Amanatides-Woo) — идёт от границы вокселя к границе,
    // а не мелкими шагами по STEP. Для maxDist=8 это ~24 проверки вместо 160.
    // maxDist по умолчанию — дальность руки (MAX_DIST); для молнии передаётся
    // отдельная, более длинная дистанция (см. LIGHTNING_RANGE).
    bool RayCast(const Camera3D& camera, World& world,
                 int& hx, int& hy, int& hz,
                 int& nx, int& ny, int& nz,
                 float maxDist = MAX_DIST) {

        Ray ray = GetMouseRay(
            { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f },
                              camera
        );

        const float INF = 1e30f;

        int x = (int)floorf(ray.position.x);
        int y = (int)floorf(ray.position.y);
        int z = (int)floorf(ray.position.z);

        int stepX = (ray.direction.x > 0.0f) - (ray.direction.x < 0.0f);
        int stepY = (ray.direction.y > 0.0f) - (ray.direction.y < 0.0f);
        int stepZ = (ray.direction.z > 0.0f) - (ray.direction.z < 0.0f);

        // tMax — расстояние вдоль луча до ближайшей границы вокселя по каждой оси
        float tMaxX = (stepX != 0) ? ((stepX > 0 ? (x + 1 - ray.position.x) : (ray.position.x - x)) / fabsf(ray.direction.x)) : INF;
        float tMaxY = (stepY != 0) ? ((stepY > 0 ? (y + 1 - ray.position.y) : (ray.position.y - y)) / fabsf(ray.direction.y)) : INF;
        float tMaxZ = (stepZ != 0) ? ((stepZ > 0 ? (z + 1 - ray.position.z) : (ray.position.z - z)) / fabsf(ray.direction.z)) : INF;

        // tDelta — на сколько t увеличивается при пересечении одного вокселя по оси
        float tDeltaX = (stepX != 0) ? (1.0f / fabsf(ray.direction.x)) : INF;
        float tDeltaY = (stepY != 0) ? (1.0f / fabsf(ray.direction.y)) : INF;
        float tDeltaZ = (stepZ != 0) ? (1.0f / fabsf(ray.direction.z)) : INF;

        int prevX = -1, prevY = -1, prevZ = -1;
        float t = 0.0f;

        while (t < maxDist) {
            if (world.IsBlockSolid(x, y, z)) {
                hx = x; hy = y; hz = z;
                nx = prevX; ny = prevY; nz = prevZ;
                return true;
            }

            prevX = x; prevY = y; prevZ = z;

            if (tMaxX < tMaxY && tMaxX < tMaxZ)      { x += stepX; t = tMaxX; tMaxX += tDeltaX; }
            else if (tMaxY < tMaxZ)                  { y += stepY; t = tMaxY; tMaxY += tDeltaY; }
            else                                      { z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; }
        }
        return false;
    }
};



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Простые частицы для эффекта попадания молнии — маленькие белые кубики,
// разлетающиеся радиально с лёгкой гравитацией и затуханием (fade) по времени жизни.
struct Particle {
    Vector3 pos;
    Vector3 velocity;
    float life;
    float maxLife;
};

void SpawnImpactParticles(std::vector<Particle>& particles, Vector3 center, int count) {
    for (int i = 0; i < count; i++) {
        float theta = (float)GetRandomValue(0, 359) * DEG2RAD;
        float phi   = (float)GetRandomValue(0, 179) * DEG2RAD;
        float speed = 2.0f + (float)GetRandomValue(0, 100) / 100.0f * 3.0f;

        Vector3 dir = { sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta) };
        Particle p;
        p.pos = center;
        p.velocity = Vector3Scale(dir, speed);
        p.velocity.y += 2.0f; // лёгкий подброс вверх, чтобы не разлетались только вниз
        p.maxLife = 0.5f + (float)GetRandomValue(0, 100) / 100.0f * 0.4f;
        p.life = p.maxLife;
        particles.push_back(p);
    }
}

void UpdateParticles(std::vector<Particle>& particles, float dt) {
    for (auto& p : particles) {
        p.velocity.y -= 9.0f * dt; // гравитация
        p.pos = Vector3Add(p.pos, Vector3Scale(p.velocity, dt));
        p.life -= dt;
    }
    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                        [](const Particle& p) { return p.life <= 0.0f; }),
        particles.end());
}

void DrawParticles(const std::vector<Particle>& particles) {
    for (auto& p : particles) {
        float t = p.life / p.maxLife;
        float size = 0.06f + 0.06f * t;
        DrawCube(p.pos, size, size, size, Fade(WHITE, t));
    }
}

// Зигзагообразный разряд молнии между двумя точками — несколько сегментов
// со случайным смещением поперёк луча (сильнее в середине, стихает к концам).
// Рисуется двумя слоями: тонкое яркое ядро + толще полупрозрачное свечение.
void DrawLightningBolt(Vector3 start, Vector3 end) {
    const int SEGMENTS = 6;

    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    Vector3 dirNorm = (len > 0.001f) ? Vector3Scale(dir, 1.0f / len) : Vector3{ 0, 0, 1 };

    Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    Vector3 perp1 = Vector3CrossProduct(dirNorm, worldUp);
    if (Vector3Length(perp1) < 0.01f) perp1 = Vector3CrossProduct(dirNorm, Vector3{ 1.0f, 0.0f, 0.0f });
    perp1 = Vector3Normalize(perp1);
    Vector3 perp2 = Vector3CrossProduct(dirNorm, perp1);

    Vector3 prev = start;
    for (int i = 1; i <= SEGMENTS; i++) {
        float t = (float)i / SEGMENTS;
        Vector3 point = Vector3Lerp(start, end, t);

        if (i != SEGMENTS) { // последняя точка не дрожит — молния должна точно попадать в цель
            float jitter = (1.0f - fabsf(t - 0.5f) * 2.0f) * 0.35f;
            float r1 = (float)GetRandomValue(-100, 100) / 100.0f * jitter;
            float r2 = (float)GetRandomValue(-100, 100) / 100.0f * jitter;
            point = Vector3Add(point, Vector3Add(Vector3Scale(perp1, r1), Vector3Scale(perp2, r2)));
        }

        DrawCylinderEx(prev, point, 0.07f, 0.07f, 6, Fade(SKYBLUE, 0.35f)); // свечение
        DrawCylinderEx(prev, point, 0.035f, 0.035f, 6, Fade(WHITE, 0.9f)); // яркое ядро
        prev = point;
    }
}


// Ищем ближайшую к (startX,startZ) колонку, свободную от препятствий (деревьев)
// на высоте героя. Без этого точка спавна выбиралась только по TerrainHeight,
// то есть игнорировала декорации — и при фиксированных координатах спавна
// детерминированно попадала в дерево, если оно там сгенерировалось.
void FindSpawnColumn(World& world, int startX, int startZ, int& outX, int& outZ) {
    for (int r = 0; r <= 24; r++) {
        for (int dx = -r; dx <= r; dx++) {
            for (int dz = -r; dz <= r; dz++) {
                int ax = dx < 0 ? -dx : dx;
                int az = dz < 0 ? -dz : dz;
                if ((ax > az ? ax : az) != r) continue; // только периметр текущего радиуса

                int tx = startX + dx, tz = startZ + dz;
                int surf = TerrainHeight(tx, tz);

                bool blocked = false;
                for (int hy = surf + 1; hy <= surf + 3; hy++) {
                    if (world.IsBlockSolid(tx, hy, tz)) { blocked = true; break; }
                }
                if (!blocked) { outX = tx; outZ = tz; return; }
            }
        }
    }
    outX = startX; outZ = startZ; // не нашли (крайне маловероятно) — используем стартовую точку
}



int main() {
    InitWindow(GetMonitorWidth(0), GetMonitorHeight(0), "Mine");
    SetWindowState(FLAG_WINDOW_UNDECORATED);
    DisableCursor();
    SetTargetFPS(165);

    gWorldSeed = (unsigned int)time(NULL); // разный мир при каждом запуске

    World world;
    // Без троттлинга и до старта игрового цикла — площадка вокруг спавна должна
    // быть полностью сгенерирована и декорирована ДО того, как мы ищем свободное место
    world.UpdateLoadedChunks(0, 0, false);

    int spawnX, spawnZ;
    FindSpawnColumn(world, 8, 8, spawnX, spawnZ);
    int spawnSurface = TerrainHeight(spawnX, spawnZ);

    Hero hero;
    hero.pos = { spawnX + 0.5f, (float)(spawnSurface + 1), spawnZ + 0.5f }; // стоим прямо на земле, без дерева над головой

    Camera3D camera = {};
    camera.up         = { 0.0f, 1.0f, 0.0f };
    camera.fovy       = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

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

    Texture2D atlasStone = LoadTexture("textures/atlas_stone.png");

    BlockTextures stoneTextures;
    stoneTextures.atlas  = atlasStone;
    stoneTextures.top    = { 0.00f, 0.0f, 0.25f, 1.0f };
    stoneTextures.side   = { 0.25f, 0.0f, 0.50f, 1.0f };
    stoneTextures.bottom = { 0.50f, 0.0f, 0.75f, 1.0f };
    world.textures[BlockType::STONE] = stoneTextures;

    Texture2D atlasLog = LoadTexture("textures/atlas_log.png");

    BlockTextures logTextures;
    logTextures.atlas  = atlasLog;
    logTextures.top    = { 0.00f, 0.0f, 0.25f, 1.0f };
    logTextures.side   = { 0.25f, 0.0f, 0.50f, 1.0f };
    logTextures.bottom = { 0.50f, 0.0f, 0.75f, 1.0f };
    world.textures[BlockType::LOG] = logTextures;

    Texture2D atlasLeaves = LoadTexture("textures/atlas_leaves.png");

    BlockTextures leavesTextures;
    leavesTextures.atlas  = atlasLeaves;
    leavesTextures.top    = { 0.00f, 0.0f, 0.25f, 1.0f };
    leavesTextures.side   = { 0.25f, 0.0f, 0.50f, 1.0f };
    leavesTextures.bottom = { 0.50f, 0.0f, 0.75f, 1.0f };
    world.textures[BlockType::LEAVES] = leavesTextures;

    // Кулдауны для ломания/установки блоков при УДЕРЖАНИИ мыши — без них при 165 FPS
    // одно нажатие превращалось бы в сотни блоков в секунду
    const float BREAK_INTERVAL = 0.25f;
    const float PLACE_INTERVAL = 0.25f;
    float breakCooldown = 0.0f;
    float placeCooldown = 0.0f;

    // Состояние молнии (Q) и частиц попадания
    std::vector<Particle> particles;
    float lightningTimer = 0.0f;
    Vector3 lightningStart = {};
    Vector3 lightningEnd = {};

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        breakCooldown -= dt;
        placeCooldown -= dt;
        lightningTimer -= dt;

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

        bool hitBlock = hero.RayCast(camera, world, hx, hy, hz, nx, ny, nz);
        if (hitBlock) {
            world.DrawSelection(hx, hy, hz);

            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && breakCooldown <= 0.0f) {
                world.BreakBlock(hx, hy, hz);
                breakCooldown = BREAK_INTERVAL;
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && placeCooldown <= 0.0f)
            if (hero.hotbar[hero.selectedSlot] != BlockType::AIR)
                if (!hero.WouldOverlapBlock(nx, ny, nz)) { // не даём поставить блок в себя
                    world.PlaceBlock(nx, ny, nz, hero.hotbar[hero.selectedSlot]);
                    placeCooldown = PLACE_INTERVAL;
                }

        // Молния (Q) — из правой руки в точку прицела, тратит ману. Дальность — по
        // дальности прорисовки (LIGHTNING_RANGE), отдельным рейкастом от того,
        // которым бьёт рука (MAX_DIST) — иначе молния не доставала бы дальше руки.
        if (IsKeyPressed(KEY_Q) && hero.mana >= LIGHTNING_MANA_COST) {
            hero.mana -= LIGHTNING_MANA_COST;

            int lhx, lhy, lhz, lnx, lny, lnz;
            bool lightningHit = hero.RayCast(camera, world, lhx, lhy, lhz, lnx, lny, lnz, LIGHTNING_RANGE);

            lightningStart = hero.GetRightHandPos(camera.position);
            lightningEnd = lightningHit
                ? Vector3{ lhx + 0.5f, lhy + 0.5f, lhz + 0.5f }
                : Vector3Add(camera.position, Vector3Scale(hero.GetForward(), LIGHTNING_RANGE));
            lightningTimer = LIGHTNING_DURATION;

            if (lightningHit) {
                // Центр всегда ломается, соседние блоки в радиусе — со случайным
                // шансом, чтобы воронка каждый раз выглядела неровной, "живой"
                world.BreakBlock(lhx, lhy, lhz);
                for (int dx = -1; dx <= 1; dx++)
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dz = -1; dz <= 1; dz++) {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            if (dx * dx + dy * dy + dz * dz > LIGHTNING_BREAK_RADIUS_SQ) continue;
                            if (GetRandomValue(0, 99) < LIGHTNING_BREAK_CHANCE)
                                world.BreakBlock(lhx + dx, lhy + dy, lhz + dz);
                        }

                SpawnImpactParticles(particles, lightningEnd, LIGHTNING_PARTICLE_COUNT);
            }
        }

        // Блоки мира
        world.UpdateLoadedChunks(playerChunkX, playerChunkZ);
        world.Draw(camera);

        hero.DrawHands(camera.position);

        UpdateParticles(particles, dt);
        DrawParticles(particles);

        if (lightningTimer > 0.0f)
            DrawLightningBolt(lightningStart, lightningEnd);

        EndMode3D();

        // HUD
        hero.HUD();

        if (IsKeyDown(KEY_ESCAPE)) break;

        EndDrawing();
    }

    UnloadTexture(atlasGrass);
    UnloadTexture(atlasDirt);
    UnloadTexture(atlasStone);
    UnloadTexture(atlasLog);
    UnloadTexture(atlasLeaves);

    CloseWindow();
    return 0;
}
