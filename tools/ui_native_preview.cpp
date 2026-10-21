// Offscreen preview of production D3D9 widgets; does not execute the GTA engine.
#include <windows.h>
#include <d3d9.h>
#include <string>
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"
#include "shared/ui/input/Input.h"
#include "shared/game/SkinCatalog.h"
namespace coop::ui { Input& In() { static Input input; return input; } }
int main(int argc, char** argv) {
    using namespace coop::ui;
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"CoopUIPreview";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"UI preview", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 900, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9* api = Direct3DCreate9(D3D_SDK_VERSION);
    if (!api || !window) return 1;
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
    pp.BackBufferWidth = 1280; pp.BackBufferHeight = 900; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev))) return 2;
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF100C18, 1, 0);
    dev->BeginScene();
    auto& g = Gfx(); if (!g.Begin(dev)) return 3;
    bool browser=argc>2 && std::string(argv[2])=="skins";
    float s = g.Scale(), width = 940*s, height = (browser ? 950 : 820)*s;
    float x = (1280-width)/2, y = (900-height)/2, cx=x+52*s, cw=width-104*s;
    w::BeginFrame(); float cy=w::Panel(x,y,width,height,"SA:COOP")+8*s;
    if (browser) {
        g.Text(FONT_BIG,cx,cy,"Your character",0xFFF6F1FF,0);cy+=g.LineHeight(FONT_BIG)+18*s;
        std::wstring search; w::TextBox("search",cx,cy,cw,36*s,search,40,false,false,"Search by name or ID");cy+=52*s;
        float cell=(cw-20*s)/3;
        for(int i=0;i<18;i++) { const auto& skin=coop::skins::kCatalog[i];
            w::Button("skin",cx+(i%3)*(cell+10*s),cy+(i/3)*69*s,cell,60*s,"#"+std::to_string(skin.id)+"  "+skin.name,true,i==0); }
        cy+=430*s;w::Selector("page",cx,cy,cw,36*s,"Page 1 / 17 · 299 skins");cy+=55*s;
        g.Text(FONT_NORMAL,cx,cy,"Selected: CJ (#0)",w::kSaBlue,0);
    } else {
        w::Tab("join",cx,cy,190*s,38*s,"Join a game",false);
        w::Tab("host",cx+206*s,cy,190*s,38*s,"Host a game",true);cy+=66*s;
        std::wstring nick=L"Alexander"; g.Text(FONT_MENU_SMALL,cx,cy,"Nickname",w::kSaMuted,0);cy+=28*s;
        w::TextBox("nick",cx,cy,320*s,38*s,nick,20);w::Button("skin",cx+340*s,cy,cw-340*s,38*s,"Grove · OG / #105");cy+=66*s;
        g.Text(FONT_MENU_SMALL,cx,cy,"Save",w::kSaMuted,0);cy+=28*s;
        w::Selector("save",cx,cy,cw,38*s,"New game");cy+=66*s;
        g.Text(FONT_MENU_SMALL,cx,cy,"Port",w::kSaMuted,0);g.Text(FONT_MENU_SMALL,cx+170*s,cy,"Password",w::kSaMuted,0);cy+=28*s;
        std::wstring port=L"7788",password;w::TextBox("port",cx,cy,150*s,38*s,port,5);w::TextBox("pass",cx+170*s,cy,cw-170*s,38*s,password,32,true,false,"optional");cy+=66*s;
        bool ff=false;w::Checkbox("ff",cx,cy,"Friendly fire",ff);cy+=58*s;
        w::Button("start",cx,cy,cw,50*s,"Start the server",true,true,true);
    }
    w::Button("close",cx+cw-130*s,y+height-90*s,130*s,38*s,"Close");
    g.Text(FONT_SMALL,cx,y+height-40*s,"F9  window     T  chat     B  voice     G  passenger     F2  players",w::kSaMuted,0);
    w::EndFrame();g.End();dev->EndScene();
    bool saved=g.SaveFrame(argc>1?argv[1]:"native-menu.bmp",1);
    g.Invalidate();dev->Release();api->Release();DestroyWindow(window);return saved?0:4;
}
