#include "send_input.h"

#include <cstdio>
#include <windows.h>

namespace
{
bool SendUnicodeText(const std::wstring &text)
{
    for (wchar_t ch : text)
    {
        INPUT input[2]{};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wScan = ch;
        input[0].ki.dwFlags = KEYEVENTF_UNICODE;

        input[1] = input[0];
        input[1].ki.dwFlags |= KEYEVENTF_KEYUP;

        const UINT sent = SendInput(2, input, sizeof(INPUT));
        if (sent != 2)
        {
            printf("[SEND_INPUT] SendInput failed for char %04X, sent: %u, error: %lu\n", static_cast<unsigned int>(ch), sent, GetLastError());
            fflush(stdout);
            return false;
        }
    }
    return true;
}

bool PasteFromClipboard(const std::wstring &text)
{
    if (text.empty())
    {
        return true;
    }

    if (!OpenClipboard(nullptr))
    {
        printf("[CLIPBOARD] OpenClipboard failed: %lu\n", GetLastError());
        fflush(stdout);
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr)
    {
        CloseClipboard();
        printf("[CLIPBOARD] GlobalAlloc failed: %lu\n", GetLastError());
        fflush(stdout);
        return false;
    }

    void *data = GlobalLock(memory);
    if (data == nullptr)
    {
        GlobalFree(memory);
        CloseClipboard();
        printf("[CLIPBOARD] GlobalLock failed: %lu\n", GetLastError());
        fflush(stdout);
        return false;
    }

    memcpy(data, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
    {
        GlobalFree(memory);
        CloseClipboard();
        printf("[CLIPBOARD] Unable to set Unicode clipboard text: %lu\n", GetLastError());
        fflush(stdout);
        return false;
    }

    CloseClipboard();

    INPUT input[4]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = 'V';
    input[2] = input[1];
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3] = input[0];
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(4, input, sizeof(INPUT));
    if (sent != 4)
    {
        printf("[CLIPBOARD] Ctrl+V SendInput failed, sent: %u, error: %lu\n", sent, GetLastError());
        fflush(stdout);
        return false;
    }
    return true;
}
} // namespace

TextOutputMethod ParseTextOutputMethod(const std::string &value)
{
    return value == "clipboard_paste" ? TextOutputMethod::ClipboardPaste : TextOutputMethod::SendInput;
}

void send_text(const std::wstring &text, TextOutputMethod method)
{
    const bool sent = method == TextOutputMethod::ClipboardPaste ? PasteFromClipboard(text) : SendUnicodeText(text);
    if (!sent && method == TextOutputMethod::ClipboardPaste)
    {
        printf("[CLIPBOARD] Falling back to Unicode SendInput.\n");
        fflush(stdout);
        SendUnicodeText(text);
    }

    printf("[SEND_INPUT] Done sending.\n");
    fflush(stdout);
}
