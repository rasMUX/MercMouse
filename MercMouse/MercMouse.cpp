#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <thread>
#include <type_traits>

using std::byte;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;

static HWND windowHandle;
static LONG_PTR originalWndProc;
static int x = 0;
static int y = 0;
static double mouseRotation;
static double mouseSensitivity;
static double zoomSensitivity;
static double invertMouse;
static constexpr std::size_t EEMemSize = 0x02884000;

static std::array<byte, EEMemSize>* EEMem{ nullptr };

struct Struct {};

template <typename T, u32 Offset = 0, bool CheckBeforeDeref = false> requires std::is_trivial_v<T>
class Ptr {
public:
    [[nodiscard]] constexpr Ptr() noexcept = default;
    [[nodiscard]] constexpr Ptr(u32 const addr) noexcept : m_addr(addr) {}

private:
    u32 m_addr;

private:
    //Metaprogramming FUN for chaining
    template <typename T_, u32, bool> requires std::is_trivial_v<T_>
    friend class Ptr;

    template <typename, template <typename, u32, bool> typename>
    struct is_instance : public std::false_type {};

    template <typename T_, u32 Offset_, bool CheckBeforeDeref_, template <typename, u32, bool> typename Ptr_>
    struct is_instance<Ptr_<T_, Offset_, CheckBeforeDeref_>, Ptr_> : public std::true_type {};

    template <typename U, template <typename, u32, bool> typename Ptr_>
    static constexpr bool is_instance_v = is_instance<U, Ptr_>::value;

public:
    template <typename Dst, u32 DstOffset = Offset, bool DstCheckBeforeDeref = CheckBeforeDeref>
    [[nodiscard]] constexpr auto cast() const noexcept -> Ptr<Dst, DstOffset, DstCheckBeforeDeref>
    {
        return { this->m_addr };
    }

    [[nodiscard]] constexpr auto offset(i32 const offset) const noexcept -> Ptr
    {
        return { static_cast<u32>(static_cast<i32>(this->m_addr) + offset) };
    }

    [[nodiscard]] constexpr auto deref() const noexcept -> T& {
        static_assert(!std::is_same_v<T, Struct>, "Attempted to dereference pointer to generic PS2 Struct");
        return *reinterpret_cast<T*>(reinterpret_cast<byte*>(EEMem) + this->m_addr + Offset);
    }

    [[nodiscard]] constexpr auto derefSafe() const noexcept -> T* {
        if constexpr (CheckBeforeDeref) {
            if (!this->isValid()) {
                return nullptr;
            }
        }
        return &this->deref();
    }

    [[nodiscard]] constexpr auto follow() const noexcept -> auto& {
        if constexpr (is_instance_v<T, Ptr>) {
            return this->deref().follow();
        }
        else {
            return this->deref();
        }
    }

    [[nodiscard]] constexpr auto followSafe() const noexcept -> auto* {
        using return_type = std::add_pointer_t<decltype(follow())>;
        auto* const value = this->derefSafe();
        if constexpr (CheckBeforeDeref) {
            if (!value) return return_type{ nullptr };
        }
        if constexpr (is_instance_v<T, Ptr>) {
            return value->followSafe();
        }
        else {
            return value;
        }
    }

    [[nodiscard]] constexpr auto isValid() const noexcept -> bool {
        if (!this->m_addr) return false;
        if (static_cast<std::size_t>(this->m_addr) + static_cast<std::size_t>(Offset) + sizeof(T) > EEMemSize) return false;
        return true;
    }

    template <typename Dst, u32 DstOffset = 0, bool DstCheckBeforeDeref = false>
    [[nodiscard]] constexpr auto chain() const noexcept -> decltype(auto) {
        if constexpr (is_instance_v<T, Ptr>) {
            return this->cast<decltype(T{}.template chain<Dst, DstOffset, DstCheckBeforeDeref>()) > ();
        }
        else {
            return this->cast<Ptr<Dst, DstOffset, DstCheckBeforeDeref>>();
        }
    }

    [[nodiscard]] constexpr auto operator*() const noexcept -> auto&
    {
        return this->follow();
    }
};

namespace INI
{
    static constexpr std::string appName("MercMouse");
    static constexpr std::string iniPath(".\\settings.ini");

    template <typename T, auto ToString, auto FromString> requires requires(T t, std::string str) {
        { ToString(t) } -> std::convertible_to<std::string>;
        { FromString(str) } -> std::convertible_to<T>;
    }
    auto ReadValue(std::string const& key, T const& defaultValue) -> T {
        char buffer[256];
        auto const length = GetPrivateProfileStringA(appName.c_str(), key.c_str(), nullptr, buffer, sizeof(buffer), iniPath.c_str());
        if (length == 0) {
            WritePrivateProfileStringA(appName.c_str(), key.c_str(), ToString(defaultValue).c_str(), iniPath.c_str());
            return defaultValue;
        }
        return FromString(std::string(buffer));
    }

    inline auto ReadString(std::string const& key, std::string const& defaultValue) -> std::string {
        return ReadValue < std::string, std::identity{}, std::identity{} > (key, defaultValue);
    }

    inline auto ReadInt(std::string const& key, int const defaultValue) -> int {
        static constexpr auto ToString = [](int const i) -> std::string { return std::to_string(i); };
        static constexpr auto FromString = [](std::string str) -> int { return std::stoi(str); };
        return ReadValue<int, ToString, FromString>(key, defaultValue);
    }

    inline auto ReadBool(std::string const& key, bool const defaultValue) -> bool {
        return static_cast<bool>(ReadInt(key, defaultValue));
    }

    inline auto ReadDouble(std::string const& key, double const defaultValue) -> double {
        static constexpr auto ToString = [](double const d) -> std::string { return std::to_string(d); };
        static constexpr auto FromString = [](std::string str) -> double { return std::stod(str); };
        return ReadValue<double, ToString, FromString>(key, defaultValue);
    }
}

static LRESULT __stdcall HookedWndProc(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    if (uMsg == WM_INPUT) {
        UINT dwSize;

        GetRawInputData(reinterpret_cast<HRAWINPUT const>(lParam), RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        LPBYTE const lpb = new BYTE[dwSize];
        if (lpb == NULL) 
        {
            return 0;
        }

        if (GetRawInputData(reinterpret_cast<HRAWINPUT const>(lParam), RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
            OutputDebugStringA("GetRawInputData does not return correct size!");

        RAWINPUT* const raw = reinterpret_cast<RAWINPUT* const>(lpb);

        if (raw->header.dwType == RIM_TYPEMOUSE) 
        {
            x += raw->data.mouse.lLastX;
            y += raw->data.mouse.lLastY;
        }

        delete[] lpb;
        return 0;
    }

    return CallWindowProc(reinterpret_cast<WNDPROC const>(originalWndProc), hWnd, uMsg, wParam, lParam); // Call the original Function
}

static void Init() {
    mouseRotation = INI::ReadDouble("DegreesPerCount", 0.022);
    mouseSensitivity = INI::ReadDouble("Sensitivity", 1.0) * std::numbers::pi / 180.0;
    zoomSensitivity = INI::ReadDouble("ZoomSensitivity", 0.5);
    invertMouse = INI::ReadBool("InvertMouse", false) ? 1.0 : -1.0;

    originalWndProc = SetWindowLongPtr(windowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc));

    HMODULE const handleToOwner = GetModuleHandleA(NULL);
    EEMem = *reinterpret_cast<decltype(EEMem)*>(GetProcAddress(handleToOwner, "EEmem"));

    RAWINPUTDEVICE const rid{
        .usUsagePage{0x1},          // HID_USAGE_PAGE_GENERIC
        .usUsage{0x2},              // HID_USAGE_GENERIC_MOUSE
        .dwFlags{0x0},              // Nothing fancy
        .hwndTarget{windowHandle}   // Hopefully it's proper enough
    };
    
    while (true) {
        OutputDebugStringA("Sleeping!");
        std::this_thread::sleep_for(std::chrono::seconds{10});

        int offset = 0;
        u64 const nameBytes = *Ptr<u64>{ 0x12613 };
        //Iffy on it but the ancient 1.2 build seems to have it here too so it's fine, probably breaks if your game isn't an iso
        switch (nameBytes) {
            case 0x32332E3930325F53ULL: //S_209.32
                break;
            case 0x38382E3532355F53ULL: //S_525.88
            case 0x30392E3532355F53ULL: //S_525.90
                offset = -0x80;
                break;
            default:
                continue;
        }

        //Probably not a good idea but it hasn't necessarily screamed at me and decreases statefulness
        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
        {
            OutputDebugStringA("Registration failed");
        }

        OutputDebugStringA("Running!");

        while (*Ptr<u64>{ 0x12613 } == nameBytes) {
            if (*Ptr<u32>{ 0x4A3908 }.offset(offset) && *Ptr<u32>{ 0x4A3914 }.offset(offset) == 0x1) {
                float const mouseMoveX = static_cast<float>(mouseRotation * mouseSensitivity * static_cast<double>(x));
                float const mouseMoveY = static_cast<float>(mouseRotation * mouseSensitivity * invertMouse * static_cast<double>(y));

                //Foot and vehicle camera
                auto const cameraInfo = Ptr<Struct>{ 0x501C04 }.offset(offset);
                //If in vehicle be polite and write the angles to where the game reads them from itself, note that 0x140 doesn't exactly signify that
                if (*cameraInfo.chain<bool, 0x140>()) {
                    *cameraInfo.chain<float, 0xF4>() += mouseMoveX;
                    *cameraInfo.chain<float, 0xE8>() += mouseMoveY;
                }
                //Force write camera angles while on foot
                else {
                    *cameraInfo.chain<float, 0xEC>() -= mouseMoveX;
                    *cameraInfo.chain<float, 0xE0>() += mouseMoveY;
                }

                //Force write scoped angles
                auto const scopeInfo = Ptr<Struct>{ 0x501C00 }.offset(offset);
                *scopeInfo.chain<float, 0x54>() += mouseMoveX * static_cast<float>(zoomSensitivity);
                *scopeInfo.chain<float, 0x50>() += mouseMoveY * static_cast<float>(zoomSensitivity);

                //Turrets?
                auto const turret = Ptr<Ptr<Ptr<Struct, 0xC>, 0x10>>{ 0x501A70 }.offset(offset);
                if (auto* const enabled = turret.chain<bool, 0x2A3, true>().followSafe(); enabled && *enabled) {
                    *turret.chain<float, 0x214>() += mouseMoveX;
                    *turret.chain<float, 0x1F8>() += mouseMoveY;
                }
            }
            x = y = 0;

            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
}

static BOOL CALLBACK enumWindowCallback(HWND const hWnd, LPARAM) {
    DWORD process_id;

    GetWindowThreadProcessId(hWnd, &process_id);
    if (process_id != GetCurrentProcessId()) {
        OutputDebugStringA("No match");
        return TRUE;
    }
    //But Why
    if (!GetWindow(hWnd, GW_OWNER)) {
        OutputDebugStringA("Window has no owner");
        return TRUE;
    }
    windowHandle = hWnd;
    OutputDebugStringA("Found HWND!");
    return FALSE;
}

static DWORD WINAPI Thread(LPVOID)
{
    while (!windowHandle) {
        OutputDebugStringA("Looking");
        EnumWindows(enumWindowCallback, NULL);
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    Init();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD const fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH)
        CreateThread(NULL, NULL, Thread, NULL, NULL, NULL);
    return TRUE;
}
