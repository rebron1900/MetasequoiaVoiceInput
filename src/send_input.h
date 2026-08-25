#pragma once

#include <string>

enum class TextOutputMethod
{
    SendInput,
    ClipboardPaste,
};

TextOutputMethod ParseTextOutputMethod(const std::string &value);
void send_text(const std::wstring &text, TextOutputMethod method = TextOutputMethod::SendInput);
