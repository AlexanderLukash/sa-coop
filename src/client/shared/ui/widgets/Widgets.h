#pragma once
// Immediate-mode co-op widgets: dark purple panels, lilac accents, rounded controls.
#include <cstdint>
#include <string>

namespace coop::ui::w {

// Shared launcher-inspired palette (legacy constant names retained).
constexpr uint32_t kSaBlue = 0xFFC1A1FF;
constexpr uint32_t kSaSlate = 0xFFBDB5CD;
constexpr uint32_t kSaMuted = 0xFF9E94B1;
constexpr uint32_t kSaGreen = 0xFF91D5B6;
constexpr uint32_t kSaRed = 0xFFFF95AC;
constexpr uint32_t kSaLine = 0xFF443653;

void BeginFrame();  // call once per frame before widgets
void EndFrame();

// Returns the y where the content starts (below the title).
float Panel(float x, float y, float w, float h, const char* asciiTitle);
void Label(float x, float y, const std::string& utf8, uint32_t argb = 0xFFE0E0E0, bool small = false);
// A menu item (text, no box). lit = drawn as the selected item, big = the large menu font. Returns true when clicked.
bool Button(const char* id, float x, float y, float w, float h, const std::string& label, bool enabled = true,
            bool lit = false, bool big = false);
bool Tab(const char* id, float x, float y, float w, float h, const std::string& label, bool active, bool small = false);
// Single-line text box. Returns true when Enter was pressed inside it.
bool TextBox(const char* id, float x, float y, float w, float h, std::wstring& value, size_t maxLen,
             bool password = false, bool digitsOnly = false, const char* placeholder = nullptr);
// "< text >" selector. Returns -1/+1 when an arrow was clicked, 0 otherwise.
int Selector(const char* id, float x, float y, float w, float h, const std::string& text);
bool Checkbox(const char* id, float x, float y, const std::string& label, bool& value);
// The game's menu pointer (a star with a long lower blade), centred on (cx, cy).
void Pointer(float cx, float cy, float size, uint32_t argb);

void SetFocus(const char* id);
bool AnyTextFocused();
void DrawCursor();
void DrawCursorAt(float x, float y);
std::wstring ClipboardText();

}  // namespace coop::ui::w
