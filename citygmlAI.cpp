// This software is based on pugixml library (http://pugixml.org).
// pugixml is Copyright (C) 2006-2018 Arseny Kapoulkine.

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <limits>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <windows.h>
#include "pugixml.hpp"

namespace fs = std::filesystem;

// ----------------------------------------------------------------
// Konfiguracja pobierana z pliku .ini
// ----------------------------------------------------------------
struct Config {
    std::vector<std::string> trackFilenames = {"EXPORT.SCN"};
    std::string terrainFilename = "teren_e3d.scm"; // Domyslnie szuka kafelkow terenu
    std::vector<std::string> inputDirs = {"CityGML-walbrzych"};
    std::string outputFilename = "citygml.scm"; // Domyslny plik wyjsciowy dla budynkow
    
    bool exportE3D = true;
    std::string outputDirE3D = "kafle_gml"; 
    double e3dTileSize = 1000.0;
    double e3dMaxDistance = 50000.0;

    double filterDistance = 2000.0; 
    bool swapGmlXy = true;        
    double minVertexDistSq = 0.01 * 0.01; 
    double minTriangleArea = 0.01;
    
    std::string texRoof = "roof/karpiowka";
    std::string texWall = "beton2";
    std::string texGround = "asphaltdark1";
    float texScale = 0.1f; 

    bool load(const std::string& iniPath) {
        std::ifstream file(iniPath);
        if (!file.is_open()) return false;
        
        std::string line;
        while (std::getline(file, line)) {
            auto commentPos = line.find('#');
            if (commentPos == std::string::npos) commentPos = line.find(';');
            if (commentPos != std::string::npos) line = line.substr(0, commentPos);

            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == '[') continue;

            auto delim = line.find('=');
            if (delim != std::string::npos) {
                std::string key = line.substr(0, delim);
                std::string val = line.substr(delim + 1);
                key.erase(key.find_last_not_of(" \t") + 1);
                val.erase(0, val.find_first_not_of(" \t"));

                try {
                    if (key == "TRACK_FILENAMES") {
                        trackFilenames.clear();
                        std::stringstream ssList(val);
                        std::string item;
                        while(std::getline(ssList, item, ',')) {
                            item.erase(0, item.find_first_not_of(" \t"));
                            item.erase(item.find_last_not_of(" \t") + 1);
                            if(!item.empty()) trackFilenames.push_back(item);
                        }
                    }
                    else if (key == "INPUT_DIRS" || key == "INPUT_DIR") {
                        inputDirs.clear();
                        std::stringstream ssList(val);
                        std::string item;
                        while(std::getline(ssList, item, ',')) {
                            item.erase(0, item.find_first_not_of(" \t"));
                            item.erase(item.find_last_not_of(" \t") + 1);
                            if(!item.empty()) inputDirs.push_back(item);
                        }
                    }
                    else if (key == "TERRAIN_FILENAME") terrainFilename = val;
                    else if (key == "OUTPUT_FILENAME") outputFilename = val;
                    else if (key == "EXPORT_E3D") exportE3D = (val == "true" || val == "1");
                    else if (key == "OUTPUT_DIR_E3D") outputDirE3D = val;
                    else if (key == "E3D_TILE_SIZE") e3dTileSize = std::stod(val);
                    else if (key == "E3D_MAX_DISTANCE") e3dMaxDistance = std::stod(val);
                    else if (key == "FILTER_DISTANCE") filterDistance = std::stod(val);
                    else if (key == "SWAP_GML_XY") swapGmlXy = (val == "true" || val == "1");
                    else if (key == "MIN_VERTEX_DIST") { double v = std::stod(val); minVertexDistSq = v * v; }
                    else if (key == "MIN_TRIANGLE_AREA") minTriangleArea = std::stod(val);
                    else if (key == "TEX_ROOF") texRoof = val;
                    else if (key == "TEX_WALL") texWall = val;
                    else if (key == "TEX_GROUND") texGround = val;
                    else if (key == "TEX_SCALE") texScale = std::stof(val);
                } catch(...) {}
            }
        }
        return true;
    }

    void print() const {
        std::cout << "=========================================\n";
        std::cout << " Wczytana konfiguracja (citygmlAI.ini):\n";
        std::cout << "=========================================\n";
        std::cout << " -> Pliki bazowe SCN: " << trackFilenames.size() << " podanych\n";
        std::cout << " -> Katalogi GML    : " << inputDirs.size() << " podanych\n";
        std::cout << " -> Plik z terenem  : " << terrainFilename << "\n";
        std::cout << " -> Eksport E3D     : " << (exportE3D ? "TAK (" + std::to_string((int)e3dTileSize) + "m)" : "NIE") << "\n";
        std::cout << " -> Folder kafli    : " << outputDirE3D << "\n";
        std::cout << " -> Plik wyjściowy  : " << outputFilename << "\n";
        std::cout << " -> Skala UV tekstur: " << texScale << "\n";
        std::cout << "=========================================" << std::endl; 
    }
} cfg;

struct Point { double x, y, z; };
struct Vector3 { double nx, ny, nz; };
struct PolyData { std::vector<Point> verts; Vector3 normal; std::string explicitType; double centerY; };

// -----------------------------------------------------------
// Klasa dla formatu E3D 
// -----------------------------------------------------------
struct E3DChunkWriter {
    std::vector<uint8_t> data;
    void writeID(const char* id) { data.push_back(id[0]); data.push_back(id[1]); data.push_back(id[2]); data.push_back(id[3]); }
    void writeString(const std::string& str) { for (char c : str) data.push_back(static_cast<uint8_t>(c)); }
    void writeU32(uint32_t val) { uint8_t* p = reinterpret_cast<uint8_t*>(&val); data.insert(data.end(), p, p + 4); }
    void writeI32(int32_t val) { writeU32(static_cast<uint32_t>(val)); }
    void writeF32(float val) { uint8_t* p = reinterpret_cast<uint8_t*>(&val); data.insert(data.end(), p, p + 4); }
    void writeZeroes(size_t count) { data.insert(data.end(), count, 0); }
    void pad() { while (data.size() % 4 != 0) data.push_back(0); }
    void finalizeLength() {
        if (data.size() >= 8) {
            uint32_t size = static_cast<uint32_t>(data.size());
            std::memcpy(&data[4], &size, 4);
        }
    }
};

// ----------------------------------------------------------------
// Struktury dla siatki E3D (Grid)
// ----------------------------------------------------------------
struct OutputVertex { Point p; Vector3 n; float u, v; };

struct TileKey {
    int x, z;
    bool operator<(const TileKey& o) const { if (x != o.x) return x < o.x; return z < o.z; }
};

std::map<TileKey, std::map<std::string, std::vector<OutputVertex>>> globalTiles;

struct VertexKey {
    float x, y, z, nx, ny, nz, u, v;
    bool operator==(const VertexKey& o) const {
        return std::abs(x - o.x) < 0.001f && std::abs(y - o.y) < 0.001f && std::abs(z - o.z) < 0.001f &&
               std::abs(nx - o.nx) < 0.001f && std::abs(ny - o.ny) < 0.001f && std::abs(nz - o.nz) < 0.001f &&
               std::abs(u - o.u) < 0.001f && std::abs(v - o.v) < 0.001f;
    }
};
struct VertexHasher {
    size_t operator()(const VertexKey& k) const { return std::hash<float>()(k.x) ^ std::hash<float>()(k.y) ^ std::hash<float>()(k.z) ^ std::hash<float>()(k.u) ^ std::hash<float>()(k.v); }
};

// ----------------------------------------------------------------
// Funkcje pomocnicze
// ----------------------------------------------------------------
double parseDouble(const std::string& str) { std::string s = str; std::replace(s.begin(), s.end(), ',', '.'); try { return std::stod(s); } catch (...) { return 0.0; } }
double distanceSquared2D(double x1, double z1, double x2, double z2) { return (x1 - x2)*(x1 - x2) + (z1 - z2)*(z1 - z2); }
double distSq3D(Point p1, Point p2) { return (p1.x - p2.x)*(p1.x - p2.x) + (p1.y - p2.y)*(p1.y - p2.y) + (p1.z - p2.z)*(p1.z - p2.z); }
double calcTriangleArea(Point a, Point b, Point c) {
    double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z, acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    return 0.5 * std::sqrt((aby*acz-abz*acy)*(aby*acz-abz*acy) + (abz*acx-abx*acz)*(abz*acx-abx*acz) + (abx*acy-aby*acx)*(abx*acy-aby*acx));
}

// ----------------------------------------------------------------
// Punkty terenu
// ----------------------------------------------------------------
struct TerrainTriangle {
    Point a, b, c; double minX, maxX, minZ, maxZ;
    void calcBounds() {
        minX = std::min({a.x, b.x, c.x}); maxX = std::max({a.x, b.x, c.x});
        minZ = std::min({a.z, b.z, c.z}); maxZ = std::max({a.z, b.z, c.z});
    }
};

class TerrainManager {
    struct CellKey { int x, z; bool operator==(const CellKey& o) const { return x == o.x && z == o.z; } };
    struct KeyHasher { size_t operator()(const CellKey& k) const { return std::hash<int>()(k.x) ^ (std::hash<int>()(k.z) << 1); } };
    
    std::unordered_map<CellKey, std::vector<TerrainTriangle>, KeyHasher> grid;
    const double CELL_SIZE = 100.0;
    size_t totalTriangles = 0;

    int getCell(double val) const { return static_cast<int>(std::floor(val / CELL_SIZE)); }

    bool isPointInTriangleXZ(double px, double pz, const Point& a, const Point& b, const Point& c) const {
        auto sign = [](double x1, double z1, double x2, double z2, double x3, double z3) { return (x1 - x3) * (z2 - z3) - (x2 - x3) * (z1 - z3); };
        double d1 = sign(px, pz, a.x, a.z, b.x, b.z), d2 = sign(px, pz, b.x, b.z, c.x, c.z), d3 = sign(px, pz, c.x, c.z, a.x, a.z);
        return !((d1 < -0.001 || d2 < -0.001 || d3 < -0.001) && (d1 > 0.001 || d2 > 0.001 || d3 > 0.001));
    }

    double getTriangleHeight(const TerrainTriangle& t, double x, double z) const {
        double det = (t.b.z - t.c.z) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.z - t.c.z);
        if (std::abs(det) < 1e-6) return std::max({t.a.y, t.b.y, t.c.y});
        double l1 = ((t.b.z - t.c.z) * (x - t.c.x) + (t.c.x - t.b.x) * (z - t.c.z)) / det;
        double l2 = ((t.c.z - t.a.z) * (x - t.c.x) + (t.a.x - t.c.x) * (z - t.c.z)) / det;
        return l1 * t.a.y + l2 * t.b.y + (1.0 - l1 - l2) * t.c.y;
    }

    void addTriangle(const Point& a, const Point& b, const Point& c) {
        TerrainTriangle tri = {a, b, c};
        tri.calcBounds();
        int minCx = getCell(tri.minX), maxCx = getCell(tri.maxX), minCz = getCell(tri.minZ), maxCz = getCell(tri.maxZ);
        for (int cx = minCx; cx <= maxCx; ++cx) {
            for (int cz = minCz; cz <= maxCz; ++cz) {
                grid[{cx, cz}].push_back(tri);
            }
        }
        totalTriangles++;
    }

    bool loadE3DChunk(const std::string& path, double offsetX, double offsetY, double offsetZ) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;

        f.seekg(0, std::ios::end);
        size_t fileSize = f.tellg();
        f.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(fileSize);
        if (!f.read(reinterpret_cast<char*>(data.data()), fileSize)) return false;

        if (data.size() < 8 || memcmp(data.data(), "E3D0", 4) != 0) return false;

        size_t pos = 8; 
        std::vector<Point> vertices;
        std::vector<uint32_t> indices;
        struct SubmodelDef { int type, vCount, vFirst, iCount, iFirst; };
        std::vector<SubmodelDef> submodels;

        while (pos + 8 <= data.size()) {
            std::string cId(reinterpret_cast<char*>(&data[pos]), 4);
            uint32_t cLen = *reinterpret_cast<uint32_t*>(&data[pos + 4]);
            if (cLen < 8 || pos + cLen > data.size()) break;

            const uint8_t* cData = &data[pos + 8];
            size_t dLen = cLen - 8;

            if (cId == "VNT0") {
                size_t num = dLen / 32;
                vertices.resize(num);
                for (size_t i = 0; i < num; ++i) {
                    float vx = *reinterpret_cast<const float*>(cData + i*32 + 0);
                    float vy = *reinterpret_cast<const float*>(cData + i*32 + 4);
                    float vz = *reinterpret_cast<const float*>(cData + i*32 + 8);
                    vertices[i] = {vx + offsetX, vy + offsetY, vz + offsetZ};
                }
            } else if (cId == "IDX4") {
                size_t num = dLen / 4;
                indices.resize(num);
                memcpy(indices.data(), cData, dLen);
            } else if (cId == "IDX2") {
                size_t num = dLen / 2;
                indices.resize(num);
                for (size_t i=0; i<num; ++i) indices[i] = *reinterpret_cast<const uint16_t*>(cData + i*2);
            } else if (cId == "IDX1") {
                size_t num = dLen / 1;
                indices.resize(num);
                for (size_t i=0; i<num; ++i) indices[i] = cData[i];
            } else if (cId == "SUB0") {
                size_t num = dLen / 256;
                for (size_t i=0; i<num; ++i) {
                    const uint8_t* s = cData + i*256;
                    SubmodelDef sm;
                    sm.type = *reinterpret_cast<const int32_t*>(s + 8);
                    sm.vCount = *reinterpret_cast<const int32_t*>(s + 28);
                    sm.vFirst = *reinterpret_cast<const int32_t*>(s + 32);
                    sm.iCount = *reinterpret_cast<const int32_t*>(s + 156);
                    sm.iFirst = *reinterpret_cast<const int32_t*>(s + 160);
                    submodels.push_back(sm);
                }
            }
            pos += cLen;
        }

        if (indices.empty()) {
            if (submodels.empty()) {
                for (size_t i=0; i+2 < vertices.size(); i+=3) {
                    addTriangle(vertices[i], vertices[i+1], vertices[i+2]);
                }
            } else {
                for (auto& sm : submodels) {
                    if (sm.type == 4 && sm.vFirst >= 0 && sm.vFirst + sm.vCount <= vertices.size()) {
                        for (int i=0; i < sm.vCount; i+=3) {
                            addTriangle(vertices[sm.vFirst + i], vertices[sm.vFirst + i+1], vertices[sm.vFirst + i+2]);
                        }
                    }
                }
            }
        } else {
            for (auto& sm : submodels) {
                if (sm.type == 4) { 
                    for (int i=0; i < sm.iCount; i+=3) {
                        size_t idx1 = sm.iFirst + i;
                        if (idx1 + 2 >= indices.size()) break;
                        uint32_t v1 = indices[idx1] + sm.vFirst;
                        uint32_t v2 = indices[idx1+1] + sm.vFirst;
                        uint32_t v3 = indices[idx1+2] + sm.vFirst;
                        if (v1 < vertices.size() && v2 < vertices.size() && v3 < vertices.size()) {
                            addTriangle(vertices[v1], vertices[v2], vertices[v3]);
                        }
                    }
                }
            }
        }
        return true;
    }

public:
    bool load(const std::string& scnPath) {
        std::cout << "\n[ETAP 1/4] Ladowanie modelu terenu (E3D/SCM): " << scnPath << "..." << std::endl;
        std::ifstream file(scnPath);
        if (!file.is_open()) return false;
        
        fs::path scmDir = fs::path(scnPath).parent_path();
        std::string line; 
        std::vector<Point> tempVerts; 
        size_t lineCount = 0;
        int loadedE3DFiles = 0;

        while (std::getline(file, line)) {
            lineCount++;
            if (lineCount % 250000 == 0) std::cout << " -> Skanowanie: linia " << lineCount << "...\r" << std::flush;
            if (line.empty()) continue;
            
            if (line.find("model") != std::string::npos && line.find(".e3d") != std::string::npos) {
                std::stringstream ss(line);
                std::string dummy, tModel, tPath;
                double nx, ny, nz, nrot;
                if (ss >> dummy >> dummy >> dummy >> dummy >> tModel >> nx >> ny >> nz >> nrot >> tPath) {
                    if (tModel == "model") {
                        fs::path e3dPath = scmDir / tPath;
                        if (!fs::exists(e3dPath)) e3dPath = tPath; 
                        
                        if (fs::exists(e3dPath)) {
                            if (loadE3DChunk(e3dPath.string(), nx, ny, nz)) loadedE3DFiles++;
                        } else {
                            std::cout << "\n[UWAGA] Nie znaleziono zadeklarowanego kafelka E3D: " << e3dPath.string() << std::endl;
                        }
                    }
                }
                continue;
            }

            if (line.find("node") != std::string::npos) continue;
            if (line.find("endtri") != std::string::npos) { tempVerts.clear(); continue; }
            
            double x, y, z;
            if (sscanf(line.c_str(), "%lf %lf %lf", &x, &y, &z) >= 3) {
                tempVerts.push_back({x, y, z});
                if (tempVerts.size() == 3) {
                    addTriangle(tempVerts[0], tempVerts[1], tempVerts[2]);
                    tempVerts.clear();
                }
            }
        }
        
        if (loadedE3DFiles > 0) {
            std::cout << "\n[ETAP 1/4] ZAKOŃCZONO. Szybko załadowano " << loadedE3DFiles << " kafelków binarnego terenu E3D." << std::endl;
        } else {
            std::cout << "\n[ETAP 1/4] ZAKOŃCZONO. Tekstowo wczytano plik .scm terenu." << std::endl;
        }
        std::cout << "Łączna ilość wczytanych trójkątów w RAM: " << totalTriangles << "\n";
        return true;
    }

    bool getHeightAt(double x, double z, double& outHeight) const {
        auto it = grid.find({getCell(x), getCell(z)});
        if (it != grid.end()) {
            for (const auto& tri : it->second) {
                if (x >= tri.minX && x <= tri.maxX && z >= tri.minZ && z <= tri.maxZ && isPointInTriangleXZ(x, z, tri.a, tri.b, tri.c)) {
                    outHeight = getTriangleHeight(tri, x, z); return true;
                }
            }
        }
        return false;
    }
};

Vector3 calculateNewellNormal(const std::vector<Point>& poly) {
    Vector3 n = {0, 0, 0};
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t next = (i + 1) % poly.size();
        n.nx += (poly[i].y - poly[next].y) * (poly[i].z + poly[next].z);
        n.ny += (poly[i].z - poly[next].z) * (poly[i].x + poly[next].x);
        n.nz += (poly[i].x - poly[next].x) * (poly[i].y + poly[next].y);
    }
    double len = std::sqrt(n.nx*n.nx + n.ny*n.ny + n.nz*n.nz);
    if (len > 0.00001) { n.nx /= len; n.ny /= len; n.nz /= len; } else { n = {0, 1, 0}; } 
    return n;
}

double getSignedArea(const std::vector<Point>& poly) {
    double area = 0.0;
    for (size_t i = 0; i < poly.size(); i++) area += (poly[(i + 1) % poly.size()].x - poly[i].x) * (poly[(i + 1) % poly.size()].y + poly[i].y);
    return area / 2.0;
}

bool isPointInTriangle(Point p, Point a, Point b, Point c) {
    auto sign = [](Point p1, Point p2, Point p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
    double d1 = sign(p, a, b), d2 = sign(p, b, c), d3 = sign(p, c, a);
    return !((d1 < -0.001 || d2 < -0.001 || d3 < -0.001) && (d1 > 0.001 || d2 > 0.001 || d3 > 0.001));
}

std::vector<int> triangulatePolygon(const std::vector<Point>& poly3d, Vector3 normal) {
    std::vector<int> indices; int n = poly3d.size();
    if (n < 3) return indices;
    std::vector<Point> poly2d(n); int dropAxis = 0; 
    if (std::abs(normal.ny) > std::abs(normal.nx) && std::abs(normal.ny) > std::abs(normal.nz)) dropAxis = 1;
    else if (std::abs(normal.nz) > std::abs(normal.nx)) dropAxis = 2;

    for(int i=0; i<n; ++i) {
        if (dropAxis == 0) poly2d[i] = {poly3d[i].y, poly3d[i].z, 0};
        else if (dropAxis == 1) poly2d[i] = {poly3d[i].x, poly3d[i].z, 0};
        else poly2d[i] = {poly3d[i].x, poly3d[i].y, 0};
    }
    if (getSignedArea(poly2d) > 0) for(auto& p : poly2d) p.x = -p.x; 
    std::vector<int> availPoints(n); for(int i=0; i<n; ++i) availPoints[i] = i;

    int count = n, prev_count = 0;
    while (count > 2) {
        if (prev_count == count) break; prev_count = count;
        for (int i = 0; i < count; ++i) {
            int i0 = availPoints[(i + count - 1) % count], i1 = availPoints[i], i2 = availPoints[(i + 1) % count];
            Point a = poly2d[i0], b = poly2d[i1], c = poly2d[i2];
            if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0.00001) continue; 
            bool isEar = true;
            for (int j = 0; j < count; ++j) {
                if (j == (i + count - 1) % count || j == i || j == (i + 1) % count) continue;
                if (isPointInTriangle(poly2d[availPoints[j]], a, b, c)) { isEar = false; break; }
            }
            if (isEar) { indices.push_back(i0); indices.push_back(i1); indices.push_back(i2); availPoints.erase(availPoints.begin() + i); count--; break; }
        }
    }
    if (indices.empty() || count > 2) { indices.clear(); for (int i = 1; i < n - 1; ++i) { indices.push_back(0); indices.push_back(i); indices.push_back(i + 1); } }
    return indices;
}

std::string getExplicitTexture(pugi::xml_node node) {
    pugi::xml_node current = node;
    for (int i = 0; i < 10; ++i) { 
        if (!current) break;
        std::string name = current.name();
        if (name.find("RoofSurface") != std::string::npos) return cfg.texRoof;
        if (name.find("GroundSurface") != std::string::npos) return cfg.texGround;
        if (name.find("WallSurface") != std::string::npos) return cfg.texWall;
        current = current.parent();
    }
    return ""; 
}

// ----------------------------------------------------------------
// GŁÓWNY KONWERTER CITYGML
// ----------------------------------------------------------------
class CityGMLConverter {
    double offsetNorth = 0.0, offsetEast = 0.0;
    std::vector<Point> trackPoints; 
    struct BoundingBox {
        double minX = 1e18, maxX = -1e18, minZ = 1e18, maxZ = -1e18;
        void update(double x, double z) {
            if (x < minX) minX = x; if (x > maxX) maxX = x;
            if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
        }
        bool isInside(double x, double z, double r) const { return (x >= minX - r && x <= maxX + r && z >= minZ - r && z <= maxZ + r); }
    } trackBounds;

public:
    bool loadContextFromSCN(const std::vector<std::string>& scnPaths) {
        if (scnPaths.empty()) return false;
        std::cout << "\n[ETAP 2/4] Skanowanie i wyliczanie wektorów torów (" << scnPaths.size() << " plików SCN)..." << std::endl;
        
        bool baseOffsetSet = false; double baseEast = 0.0, baseNorth = 0.0;

        for (size_t i = 0; i < scnPaths.size(); ++i) {
            std::cout << " -> Skanowanie pliku SCN: " << scnPaths[i] << "..." << std::flush;
            std::ifstream file(scnPaths[i]);
            if (!file.is_open()) { std::cout << " BŁĄD OTWARCIA!" << std::endl; continue; }
            
            double modEast = 0.0, modNorth = 0.0; bool modOffsetFound = false; std::string line;
            while (std::getline(file, line)) {
                if (line.find("//$g") != std::string::npos) {
                    std::stringstream ss(line); std::string t; std::vector<double> n;
                    while (ss >> t) { try { n.push_back(std::stod(t)); } catch(...) {} }
                    if (n.size() >= 2) { modEast = n[0] * 1000.0; modNorth = n[1] * 1000.0; modOffsetFound = true; break; }
                }
            }
            if (!modOffsetFound) { std::cout << " Brak offsetu!" << std::endl; continue; }
            if (!baseOffsetSet) {
                baseEast = modEast; baseNorth = modNorth;
                this->offsetEast = baseEast; this->offsetNorth = baseNorth; baseOffsetSet = true;
            }

            double deltaX = baseEast - modEast; double deltaZ = modNorth - baseNorth;
            file.clear(); file.seekg(0); bool insideTrack = false; int coordIdx = 0;
            int addedNodes = 0;

            while (std::getline(file, line)) {
                if (!insideTrack && line.find("node") == 0 && (line.find("track") != std::string::npos || line.find("switch") != std::string::npos)) {
                    insideTrack = true; coordIdx = 0; 
                } else if (insideTrack && line.find("endtrack") == 0) {
                    insideTrack = false;
                } else if (insideTrack) {
                    size_t f = line.find_first_not_of(" \t");
                    if (f == std::string::npos || isalpha(line[f])) continue; 
                    
                    double x, y, z;
                    if (sscanf(line.c_str(), "%lf %lf %lf", &x, &y, &z) >= 3) {
                        // Explicit check dla punktów absolutnych torów i rozjazdów (omijamy wektory krzywizny!)
                        if (coordIdx == 0 || coordIdx == 3 || coordIdx == 4 || coordIdx == 7) {
                            x += deltaX; z += deltaZ;
                            if (std::abs(x) > 0.1) { trackPoints.push_back({x, y, z}); trackBounds.update(x, z); addedNodes++; }
                        }
                        coordIdx++;
                    }
                }
            }
            std::cout << " OK. (+" << addedNodes << " punktów)" << std::endl;
        }
        std::cout << "[ETAP 2/4] ZAKOŃCZONO." << std::endl;
        return baseOffsetSet;
    }

    Point transform(double geoN, double geoE, double geoH) { return { offsetEast - geoE, geoH, geoN - offsetNorth }; }

    int processFile(const std::string& gmlPath, const TerrainManager& terrainMgr) {
        pugi::xml_document doc;
        if (!doc.load_file(gmlPath.c_str())) return 0;
        
        auto buildings = doc.select_nodes("//*[local-name()='Building']");
        int addedBuildings = 0;
        
        for (auto& bNode : buildings) {
            auto thematicNodes = bNode.node().select_nodes(".//*[local-name()='RoofSurface' or local-name()='WallSurface' or local-name()='GroundSurface' or local-name()='FloorSurface']");
            bool isLoD2 = (thematicNodes.size() > 0);
            std::vector<pugi::xml_node> surfacesToProcess;
            
            if (isLoD2) {
                for (auto& t : thematicNodes) surfacesToProcess.push_back(t.node());
            } else {
                auto solidNodes = bNode.node().select_nodes(".//*[local-name()='posList']");
                for (auto& s : solidNodes) surfacesToProcess.push_back(s.node().parent());
            }
            if (surfacesToProcess.empty()) continue;

            std::vector<PolyData> polys;
            double buildMinY = 1e18, buildMinX = 1e18, buildMaxX = -1e18, buildMinZ = 1e18, buildMaxZ = -1e18;
            Point firstPoint = {0,0,0}; bool firstPointSet = false;

            for (auto& surface : surfacesToProcess) {
                std::vector<double> raw;
                pugi::xml_node posList = surface.select_node(".//*[local-name()='posList']").node();
                if (posList) {
                    std::stringstream ss(posList.child_value()); std::string t;
                    while (ss >> t) raw.push_back(parseDouble(t));
                } else {
                    auto posNodes = surface.select_nodes(".//*[local-name()='pos']");
                    for (auto& posNode : posNodes) {
                        std::stringstream ss(posNode.node().child_value()); std::string t;
                        while (ss >> t) raw.push_back(parseDouble(t));
                    }
                }
                if (raw.size() < 9) continue;

                std::vector<Point> p_vec; double localMinY = 1e18;
                for(size_t i=0; i<raw.size(); i+=3) {
                    Point p = cfg.swapGmlXy ? transform(raw[i+1], raw[i], raw[i+2]) : transform(raw[i], raw[i+1], raw[i+2]);
                    if (!p_vec.empty() && distSq3D(p_vec.back(), p) < cfg.minVertexDistSq) continue;
                    p_vec.push_back(p);
                    if (!firstPointSet) { firstPoint = p; firstPointSet = true; }
                    if (p.y < buildMinY) buildMinY = p.y; 
                    if (p.y < localMinY) localMinY = p.y;
                    if (p.x < buildMinX) buildMinX = p.x; if (p.x > buildMaxX) buildMaxX = p.x;
                    if (p.z < buildMinZ) buildMinZ = p.z; if (p.z > buildMaxZ) buildMaxZ = p.z;
                }
                while (p_vec.size() > 3 && distSq3D(p_vec.front(), p_vec.back()) < cfg.minVertexDistSq) p_vec.pop_back();
                if (p_vec.size() < 3) continue;

                Vector3 n = calculateNewellNormal(p_vec);
                polys.push_back({p_vec, n, getExplicitTexture(surface), localMinY});
            }

            if (polys.empty() || !trackBounds.isInside(firstPoint.x, firstPoint.z, cfg.filterDistance)) continue;
            
            bool isNear = false;
            for(const auto& tp : trackPoints) {
                if (distanceSquared2D(firstPoint.x, firstPoint.z, tp.x, tp.z) < (cfg.filterDistance * cfg.filterDistance)) { isNear = true; break; }
            }
            if (!isNear) continue;

            double centerX = (buildMinX + buildMaxX) / 2.0;
            double centerZ = (buildMinZ + buildMaxZ) / 2.0;
            double terrainY = 0.0;
            
            if (terrainMgr.getHeightAt(centerX, centerZ, terrainY)) {
                double shiftY = terrainY - buildMinY; 
                for (auto& pd : polys) {
                    pd.centerY += shiftY;
                    for (auto& v : pd.verts) v.y += shiftY; 
                }
                buildMinY += shiftY; 
            }

            int tileX = static_cast<int>(std::floor(centerX / cfg.e3dTileSize));
            int tileZ = static_cast<int>(std::floor(centerZ / cfg.e3dTileSize));

            for (auto& pd : polys) {
                std::string tex = cfg.texWall; bool flipWinding = false;
                if (!pd.explicitType.empty()) {
                    tex = pd.explicitType;
                } else {
                    if (std::abs(pd.normal.ny) > 0.7) { 
                        if (pd.centerY <= buildMinY + 0.5) tex = cfg.texGround;
                        else { tex = cfg.texRoof; if (pd.normal.ny < 0) flipWinding = true; }
                    }
                }
                if (tex == cfg.texRoof && pd.normal.ny < -0.1) flipWinding = true;
                if (flipWinding) { pd.normal.nx = -pd.normal.nx; pd.normal.ny = -pd.normal.ny; pd.normal.nz = -pd.normal.nz; }

                std::vector<int> indices = triangulatePolygon(pd.verts, pd.normal);
                if (flipWinding) std::reverse(indices.begin(), indices.end());

                float scale = cfg.texScale;
                float tx = 1.0f, tz = 0.0f;
                bool isWall = std::abs(pd.normal.ny) <= 0.7;

                if (isWall) {
                    tx = -pd.normal.nz;
                    tz = pd.normal.nx;
                    float tLen = std::sqrt(tx*tx + tz*tz);
                    if (tLen > 0.0001f) { tx /= tLen; tz /= tLen; } else { tx = 1.0f; tz = 0.0f; }
                }

                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    Point pA = pd.verts[indices[i]], pB = pd.verts[indices[i+1]], pC = pd.verts[indices[i+2]];
                    if (calcTriangleArea(pA, pB, pC) < cfg.minTriangleArea) continue;
                    
                    auto calcUV = [&](Point p) -> std::pair<float, float> {
                        if (!isWall) return { static_cast<float>(p.x * scale), static_cast<float>(p.z * scale) };
                        return { static_cast<float>((p.x * tx + p.z * tz) * scale), static_cast<float>(p.y * scale) };
                    };

                    auto uvA = calcUV(pA);
                    globalTiles[{tileX, tileZ}][tex].push_back({pA, pd.normal, uvA.first, uvA.second});
                    
                    auto uvB = calcUV(pB);
                    globalTiles[{tileX, tileZ}][tex].push_back({pB, pd.normal, uvB.first, uvB.second});
                    
                    auto uvC = calcUV(pC);
                    globalTiles[{tileX, tileZ}][tex].push_back({pC, pd.normal, uvC.first, uvC.second});
                }
            }
            addedBuildings++;
        }
        return addedBuildings;
    }
};

// ----------------------------------------------------------------
// Eksport do formatu .e3d
// ----------------------------------------------------------------
void ExportTilesToE3D() {
    std::cout << "\n[ETAP 4/4] Podział i zapis binarnego formatu E3D (" << cfg.e3dTileSize << "x" << cfg.e3dTileSize << "m)..." << std::endl;
    
    if (!fs::exists(cfg.outputDirE3D)) fs::create_directories(cfg.outputDirE3D);
    
    std::ofstream masterScn(cfg.outputFilename);
    masterScn << "// Wygenerowano narzędziem citygmlAI\n";

    float maxDistSq = static_cast<float>(cfg.e3dMaxDistance * cfg.e3dMaxDistance);
    int tileCounter = 0;
    int totalTiles = globalTiles.size();

    for (auto& kvTile : globalTiles) {
        int tx = kvTile.first.x;
        int tz = kvTile.first.z;
        auto& texturesMap = kvTile.second;

        double centerPosX = tx * cfg.e3dTileSize + cfg.e3dTileSize * 0.5;
        double centerPosZ = tz * cfg.e3dTileSize + cfg.e3dTileSize * 0.5;

        // Prawidłowa nazwa identyfikująca węzeł terenu w formacie kilometrowym
        int kmX = 500 + static_cast<int>(std::floor(centerPosX / 1000.0));
        int kmZ = 500 + static_cast<int>(std::floor(centerPosZ / 1000.0));
        char subNameBuffer[16];
        snprintf(subNameBuffer, sizeof(subNameBuffer), "%03d%03d", kmX, kmZ);
        std::string terrainSubName = subNameBuffer;

        std::string tileBaseName = "kafel_" + std::to_string(tx) + "_" + std::to_string(tz);
        std::string tileFileName = cfg.outputDirE3D + "/" + tileBaseName + ".e3d";

        std::vector<std::string> textures;
        for (const auto& kvTex : texturesMap) textures.push_back(kvTex.first);

        E3DChunkWriter tex0, nam0, vnt0, idx4, sub0, e3d0;

        tex0.writeID("TEX0"); tex0.writeU32(0); tex0.data.push_back(0); 
        for (const auto& tName : textures) { tex0.writeString(tName); tex0.data.push_back(0); }
        tex0.pad(); tex0.finalizeLength();

        nam0.writeID("NAM0"); nam0.writeU32(0);
        nam0.writeString(terrainSubName); 
        nam0.data.push_back(0); 
        nam0.pad(); nam0.finalizeLength();

        vnt0.writeID("VNT0"); vnt0.writeU32(0);
        idx4.writeID("IDX4"); idx4.writeU32(0);
        sub0.writeID("SUB0"); sub0.writeU32(0);

        int currentVntSize = 0, currentIdxSize = 0;

        for (size_t i = 0; i < textures.size(); ++i) {
            const auto& tris = texturesMap[textures[i]];
            std::unordered_map<VertexKey, int, VertexHasher> localVerts;
            
            int firstVertexOffset = currentVntSize;
            int firstIndexOffset = currentIdxSize;

            for (const auto& v : tris) {
                float vx = static_cast<float>(v.p.x - centerPosX);
                float vy = static_cast<float>(v.p.y);
                float vz = static_cast<float>(v.p.z - centerPosZ);

                VertexKey vk = {
                    vx, vy, vz,
                    static_cast<float>(v.n.nx), static_cast<float>(v.n.ny), static_cast<float>(v.n.nz), v.u, v.v
                };

                if (localVerts.find(vk) == localVerts.end()) {
                    localVerts[vk] = static_cast<int>(localVerts.size());
                    vnt0.writeF32(vk.x); vnt0.writeF32(vk.y); vnt0.writeF32(vk.z);
                    vnt0.writeF32(vk.nx); vnt0.writeF32(vk.ny); vnt0.writeF32(vk.nz);
                    vnt0.writeF32(vk.u); vnt0.writeF32(vk.v);
                    currentVntSize++;
                }
                idx4.writeU32(static_cast<uint32_t>(localVerts[vk]));
                currentIdxSize++;
            }

            // --- ZAPIS BINARNY STRUKTURY E3D SUB0 ---
            sub0.writeI32((i + 1 < textures.size()) ? static_cast<int32_t>(i + 1) : -1); 
            sub0.writeI32(-1); 
            sub0.writeI32(4);  // GL_TRIANGLES
            sub0.writeI32(0);  // Numer nazwy wskazujący na NAM0
            sub0.writeI32(0);  
            
            // Flaga cyklu Opaque (16)
            sub0.writeI32(16);
            
            sub0.writeI32(-1); 
            sub0.writeI32(static_cast<int32_t>(localVerts.size())); 
            sub0.writeI32(firstVertexOffset); 
            sub0.writeI32(static_cast<int32_t>(i + 1)); 

            // Zerowe parametry
            sub0.writeF32(0.0f); sub0.writeF32(0.0f); 
            sub0.writeF32(0.0f); sub0.writeF32(0.0f); sub0.writeF32(0.0f); sub0.writeF32(0.0f); 
            sub0.writeF32(1.0f); sub0.writeF32(1.0f); sub0.writeF32(1.0f); sub0.writeF32(1.0f); 
            sub0.writeF32(0.0f); sub0.writeF32(0.0f); sub0.writeF32(0.0f); sub0.writeF32(0.0f); 
            sub0.writeF32(0.0f); sub0.writeF32(0.0f); sub0.writeF32(0.0f); sub0.writeF32(0.0f); 

            sub0.writeF32(1.0f); 
            sub0.writeF32(maxDistSq); 
            sub0.writeF32(0.0f); 

            for(int m = 0; m < 8; ++m) sub0.writeF32(0.0f);

            sub0.writeI32(currentIdxSize - firstIndexOffset); 
            sub0.writeI32(firstIndexOffset); 
            sub0.writeF32(1.0f); 
            sub0.writeZeroes(88); 
        }

        vnt0.pad(); vnt0.finalizeLength();
        idx4.pad(); idx4.finalizeLength();
        sub0.finalizeLength();

        e3d0.writeID("E3D0"); 
        e3d0.writeU32(8 + static_cast<uint32_t>(sub0.data.size() + vnt0.data.size() + idx4.data.size() + tex0.data.size() + nam0.data.size())); 
        e3d0.data.insert(e3d0.data.end(), sub0.data.begin(), sub0.data.end()); 
        e3d0.data.insert(e3d0.data.end(), vnt0.data.begin(), vnt0.data.end()); 
        e3d0.data.insert(e3d0.data.end(), idx4.data.begin(), idx4.data.end()); 
        e3d0.data.insert(e3d0.data.end(), tex0.data.begin(), tex0.data.end()); 
        e3d0.data.insert(e3d0.data.end(), nam0.data.begin(), nam0.data.end());

        std::ofstream file(tileFileName, std::ios::binary); 
        file.write(reinterpret_cast<const char*>(e3d0.data.data()), e3d0.data.size()); 
        file.close(); 

        masterScn << "node -1 0 " << tileBaseName << " model " 
                  << std::fixed << std::setprecision(2) << centerPosX << " 0.0 " << centerPosZ 
                  << " 0.0 katalog/" << cfg.outputDirE3D << "/" << tileBaseName << ".e3d none endmodel\n";

        tileCounter++;
        std::cout << " -> Zapisano kafelków: " << tileCounter << " z " << totalTiles << "...\r" << std::flush;
    }
    masterScn.close();
    std::cout << "\n[ETAP 4/4] ZAKONCZONO." << std::endl;
}

// ----------------------------------------------------------------
// Main v173
// ----------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Wymuszenie kodowania UTF-8 na wyjściu konsoli Windows
    SetConsoleOutputCP(CP_UTF8);
    if (!cfg.load("citygmlAI.ini")) return 1;
    cfg.print();

    TerrainManager terrain;
    if (!terrain.load(cfg.terrainFilename)) {
        std::cerr << "UWAGA: Nie udało się otworzyć pliku terenu: " << cfg.terrainFilename << "!" << std::endl;
    }

    CityGMLConverter conv;
    if (!conv.loadContextFromSCN(cfg.trackFilenames)) {
        std::cerr << "Nie udalo sie zaladować plików SCN." << std::endl;
        return 1;
    }

    std::cout << "\n[ETAP 3/4] Wyszukiwanie, parsowanie i ładowanie modeli z katalogów CityGML..." << std::endl;
    std::set<std::string> processedFiles;
    int totalParsedBuildings = 0;

    for (const auto& dirPath : cfg.inputDirs) {
        if (!fs::exists(dirPath)) {
            std::cout << " -> Pomijam brakujący katalog: " << dirPath << std::endl;
            continue;
        }
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".gml" || ext == ".xml") {
                std::string fileName = entry.path().filename().string();
                if (processedFiles.find(fileName) != processedFiles.end()) continue;
                processedFiles.insert(fileName);
                
                std::cout << " -> Skanowanie pliku: " << fileName << " ... " << std::flush;
                int added = conv.processFile(entry.path().string(), terrain);
                std::cout << "Zaakceptowano budynków: " << added << std::endl;
                totalParsedBuildings += added;
            }
        }
    }
    std::cout << "[ETAP 3/4] ZAKOŃCZONO. Gotowych budynków w RAM: " << totalParsedBuildings << std::endl;

    if (cfg.exportE3D) {
        ExportTilesToE3D();
    }
    
    std::cout << "Wszystkie operacje przebiegły pomyślnie!" << std::endl;
    std::cout << "Naciśnij klawisz Enter, aby zakończyć..." << std::endl;
    std::cin.get();
    return 0;
}