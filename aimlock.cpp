#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <Windows.h>

#define PI 3.14159265358979323846

struct Vector3 {
    float x, y, z;

    Vector3() : x(0.f), y(0.f), z(0.f) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    float Distance(const Vector3& other) const {
        return sqrt(pow(x - other.x, 2) + pow(y - other.y, 2) + pow(z - other.z, 2));
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }
};

struct Player {
    uintptr_t baseAddress;
    bool isAlive;
    bool isEnemy;
    Vector3 position;
    Vector3 viewAngles;

    explicit Player(uintptr_t addr) : baseAddress(addr), isAlive(false), isEnemy(false) {}
};

class ValorantAimlock {
private:
    const float MAX_TARGET_DISTANCE = 20.f;
    const DWORD PROCESS_ID = GetValorantProcessId();
    const uintptr_t GAME_MODULE = GetGameModuleAddress(PROCESS_ID, L"VALORANT-Win64-Shipping.exe");
    
    HANDLE hProcess = nullptr;
    bool isRunning = false;
    Vector3 localPlayerView;

    static DWORD GetValorantProcessId() {
        DWORD pid = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            
            if (Process32First(hSnap, &pe32)) {
                do {
                    if (!wcscmp(pe32.szExeFile, L"VALORANT-Win64-Shipping.exe")) {
                        pid = pe32.th32ProcessID;
                        break;
                    }
                } while (Process32Next(hSnap, &pe32));
            }
            CloseHandle(hSnap);
        }
        return pid;
    }

    static uintptr_t GetGameModuleAddress(DWORD pid, const wchar_t* moduleName) {
        uintptr_t addr = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        
        if (hSnap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 me32;
            me32.dwSize = sizeof(MODULEENTRY32);
            
            if (Module32First(hSnap, &me32)) {
                do {
                    if (!wcscmp(me32.szModule, moduleName)) {
                        addr = reinterpret_cast<uintptr_t>(me32.modBaseAddr);
                        break;
                    }
                } while (Module32Next(hSnap, &me32));
            }
            CloseHandle(hSnap);
        }
        return addr;
    }

    template <typename T>
    T ReadMemory(uintptr_t address) const {
        T buffer;
        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &buffer, sizeof(T), nullptr);
        return buffer;
    }

    template <typename T>
    void WriteMemory(uintptr_t address, const T& value) const {
        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address), &value, sizeof(T), nullptr);
    }

    Player GetLocalPlayer() const {
        const uintptr_t localPlayerAddr = ReadMemory<uintptr_t>(GAME_MODULE + 0x14F4A70);
        Player local(localPlayerAddr);
        
        if (local.baseAddress) {
            local.isAlive = ReadMemory<bool>(local.baseAddress + 0x378);
            local.position = ReadMemory<Vector3>(local.baseAddress + 0x15C);
            local.viewAngles = ReadMemory<Vector3>(local.baseAddress + 0x168);
        }
        return local;
    }

    std::vector<Player> GetEnemyPlayers() const {
        std::vector<Player> enemies;
        const uintptr_t playerListAddr = ReadMemory<uintptr_t>(GAME_MODULE + 0x150F5E8);
        const int playerCount = ReadMemory<int>(GAME_MODULE + 0x150F5F0);

        for (int i = 0; i < playerCount; ++i) {
            const uintptr_t playerAddr = ReadMemory<uintptr_t>(playerListAddr + i * 0x8);
            if (!playerAddr) continue;

            Player enemy(playerAddr);
            enemy.isAlive = ReadMemory<bool>(enemy.baseAddress + 0x378);
            enemy.isEnemy = ReadMemory<bool>(enemy.baseAddress + 0x380);
            enemy.position = ReadMemory<Vector3>(enemy.baseAddress + 0x15C);

            if (enemy.isAlive && enemy.isEnemy) {
                enemies.push_back(enemy);
            }
        }
        return enemies;
    }

    Vector3 CalculateAngles(const Vector3& from, const Vector3& to) const {
        const Vector3 delta = to - from;
        const float hypotenuse = sqrt(delta.x * delta.x + delta.y * delta.y);
        
        Vector3 angles;
        angles.x = atan2(-delta.z, hypotenuse) * (180.f / PI);
        angles.y = atan2(delta.y, delta.x) * (180.f / PI);
        angles.z = 0.f;

        return angles;
    }

    void NormalizeAngles(Vector3& angles) const {
        while (angles.x > 89.f) angles.x -= 180.f;
        while (angles.x < -89.f) angles.x += 180.f;
        while (angles.y > 180.f) angles.y -= 360.f;
        while (angles.y < -180.f) angles.y += 360.f;
    }

    void LockAim(const Player& local, const Player& target) const {
        Vector3 aimAngles = CalculateAngles(local.position, target.position);
        NormalizeAngles(aimAngles);
        
        WriteMemory<Vector3>(local.baseAddress + 0x168, aimAngles);
    }

public:
    ValorantAimlock() {
        if (PROCESS_ID && GAME_MODULE) {
            hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PROCESS_ID);
            isRunning = (hProcess != nullptr);
        }
    }

    ~ValorantAimlock() {
        if (hProcess) CloseHandle(hProcess);
    }

    void Run() {
        if (!isRunning) {
            std::cerr << "Failed to initialize - Valorant process not found or access denied\n";
            return;
        }

        std::cout << "Aimlock active - Press END to stop\n";
        
        while (isRunning) {
            if (GetAsyncKeyState(VK_END) & 0x8000) {
                isRunning = false;
                break;
            }

            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            const Player local = GetLocalPlayer();
            if (!local.isAlive) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const std::vector<Player> enemies = GetEnemyPlayers();
            for (const auto& enemy : enemies) {
                if (local.position.Distance(enemy.position) <= MAX_TARGET_DISTANCE) {
                    LockAim(local, enemy);
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }

        std::cout << "Aimlock stopped\n";
    }
};

int main() {
    SetConsoleTitleA("Valorant Aim Config");
    ValorantAimlock aimlock;
    aimlock.Run();
    return 0;
}
