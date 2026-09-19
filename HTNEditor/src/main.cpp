// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Translator/HTNCompilerToolingModel.h"
#include "HTNCodeGenerationPanel.h"

#include "SDL.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <commdlg.h>

#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <optional>
#include <vector>

namespace
{
struct EditorDocument
{
    int DocumentId = 0;
    std::filesystem::path FilePath;
    std::string Text;
    std::string CompileMessage = "Open a .domain file or create a new one.";
    bool CompileSucceeded = false;
    bool HasCompileResult = false;
    bool Dirty = false;
    int ErrorLine = -1;
    int ErrorColumn = 1;
    int PendingGotoLine = -1;
    size_t CursorOffset = 0;
    bool PendingLineHome = false;
    bool PendingLineEnd = false;
    bool PendingLineNavigationExtendSelection = false;
    bool PendingPageUp = false;
    bool PendingPageDown = false;
    bool PendingPageNavigationExtendSelection = false;
    bool PendingGotoDefinitionKey = false;
    bool PendingCompileShortcut = false;
    bool PendingSaveShortcut = false;
    bool PendingFindShortcut = false;
    bool PendingClipboardFindNext = false;
    bool FindBarOpen = false;
    bool PendingFindFocus = false;
    bool ForceCaretFollow = false;
    std::optional<size_t> PendingScrollToOffset;
    int PendingScrollDelayFrames = 0;
    std::string FindText;
    std::string LastSearchQuery;
    size_t LastSearchEnd = 0;
    std::optional<size_t> PendingDefinitionLookupOffset;
    std::optional<size_t> PendingGotoOffset;
    std::optional<size_t> PendingSelectionStart;
    std::optional<size_t> PendingSelectionEnd;
    HTNCompilerToolingModel ToolingModel;
};

struct EditorAppState
{
    std::vector<EditorDocument> Documents;
    int ActiveDocumentIndex = -1;
    int NextDocumentId = 1;
    int PendingSelectDocumentId = -1;
    HTNCodeGenerationPanelState CodeGenerationPanel;
};

void AnalyzeDocument(EditorDocument& inDocument, const std::string& inText)
{
    inDocument.ToolingModel.Analyze(inDocument.FilePath, inText);
}

bool ReadTextFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

#ifdef _WIN32
std::filesystem::path ShowFileDialog(bool save)
{
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "HTN Domains (*.domain)\0*.domain\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    ofn.lpstrDefExt = "domain";
    const BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    return ok ? std::filesystem::path(filename) : std::filesystem::path{};
}
#endif

bool Save(EditorDocument& state, bool saveAs)
{
#ifdef _WIN32
    if (saveAs || state.FilePath.empty())
    {
        const auto path = ShowFileDialog(true);
        if (path.empty()) return false;
        state.FilePath = path;
    }
#else
    if (state.FilePath.empty()) return false;
#endif
    if (!WriteTextFile(state.FilePath, state.Text))
    {
        state.HasCompileResult = true;
        state.CompileSucceeded = false;
        state.CompileMessage = "Could not save: " + state.FilePath.string();
        return false;
    }
    state.Dirty = false;
    AnalyzeDocument(state, state.Text);
    return true;
}

bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b)
{
    std::error_code ecA, ecB;
    const auto ca = std::filesystem::weakly_canonical(a, ecA);
    const auto cb = std::filesystem::weakly_canonical(b, ecB);
    return (ecA ? a.lexically_normal() : ca) == (ecB ? b.lexically_normal() : cb);
}

int LineFromOffset(const std::string& text, size_t offset)
{
    offset = std::min(offset, text.size());
    return 1 + static_cast<int>(
        std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

void QueueEditorNavigation(
    EditorDocument& document,
    size_t cursorOffset,
    std::optional<size_t> selectionStart = {},
    std::optional<size_t> selectionEnd = {})
{
    cursorOffset = std::min(cursorOffset, document.Text.size());
    document.PendingGotoLine = LineFromOffset(document.Text, cursorOffset);
    document.PendingGotoOffset = cursorOffset;
    document.PendingSelectionStart = selectionStart;
    document.PendingSelectionEnd = selectionEnd;
    document.PendingScrollToOffset = cursorOffset;
    document.PendingScrollDelayFrames = 1;
    // Programmatic navigation owns viewport movement. Do not let the normal caret-follow
    // path race with it while the target document is establishing its scroll range.
    document.ForceCaretFollow = false;
}

bool FindNextInDocument(EditorDocument& document, const std::string& query)
{
    if (query.empty() || document.Text.empty())
        return false;

    size_t searchStart = document.CursorOffset;
    if (document.LastSearchQuery == query)
        searchStart = std::min(document.LastSearchEnd, document.Text.size());

    size_t match = document.Text.find(query, searchStart);
    if (match == std::string::npos && searchStart > 0)
        match = document.Text.find(query, 0);

    if (match == std::string::npos)
    {
        document.HasCompileResult = true;
        document.CompileSucceeded = false;
        document.CompileMessage = "Search text not found: " + query;
        document.ErrorLine = -1;
        document.ErrorColumn = 1;
        return false;
    }

    document.LastSearchQuery = query;
    document.LastSearchEnd = match + query.size();
    QueueEditorNavigation(
        document,
        match,
        match,
        match + query.size());
    return true;
}

int FindOpenDocumentIndex(const EditorAppState& app, const std::filesystem::path& filePath)
{
    for (size_t index = 0; index < app.Documents.size(); ++index)
    {
        if (!app.Documents[index].FilePath.empty() && SameFile(app.Documents[index].FilePath, filePath))
            return static_cast<int>(index);
    }
    return -1;
}

int OpenDocument(EditorAppState& app, const std::filesystem::path& filePath)
{
    const int existing = FindOpenDocumentIndex(app, filePath);
    if (existing >= 0)
    {
        app.ActiveDocumentIndex = existing;
        app.PendingSelectDocumentId = app.Documents[existing].DocumentId;
        return existing;
    }

    std::string text;
    if (!ReadTextFile(filePath, text))
        return -1;

    EditorDocument document;
    document.DocumentId = app.NextDocumentId++;
    document.FilePath = filePath;
    document.Text = std::move(text);
    document.CompileMessage = "Opened domain.";
    AnalyzeDocument(document, document.Text);

    app.Documents.emplace_back(std::move(document));
    app.ActiveDocumentIndex = static_cast<int>(app.Documents.size()) - 1;
    app.PendingSelectDocumentId = app.Documents.back().DocumentId;
    return app.ActiveDocumentIndex;
}

int CreateNewDocument(EditorAppState& app)
{
    EditorDocument document;
    document.DocumentId = app.NextDocumentId++;
    document.Text = "";
    document.Dirty = true;
    document.CompileMessage = "New unsaved domain.";
    AnalyzeDocument(document, document.Text);

    app.Documents.emplace_back(std::move(document));
    app.ActiveDocumentIndex = static_cast<int>(app.Documents.size()) - 1;
    app.PendingSelectDocumentId = app.Documents.back().DocumentId;
    return app.ActiveDocumentIndex;
}

void GoToDefinition(EditorAppState& app, const int sourceDocumentIndex, size_t sourceOffset)
{
    if (sourceDocumentIndex < 0 || sourceDocumentIndex >= static_cast<int>(app.Documents.size()))
        return;

    // Resolve before opening another document: adding a vector element may move the source
    // document and invalidate references into it.
    HTNCompilerToolingDefinition definition;
    if (!app.Documents[sourceDocumentIndex].ToolingModel.GetDefinitionAtOffset(sourceOffset, definition))
    {
        EditorDocument& source = app.Documents[sourceDocumentIndex];
        source.HasCompileResult = true;
        source.CompileSucceeded = false;
        source.CompileMessage = "No definition found for symbol under cursor.";
        source.ErrorLine = -1;
        source.ErrorColumn = 1;
        return;
    }

    int targetIndex = sourceDocumentIndex;
    if (!SameFile(app.Documents[sourceDocumentIndex].FilePath, definition.FilePath))
    {
        targetIndex = OpenDocument(app, definition.FilePath);
        if (targetIndex < 0)
        {
            EditorDocument& source = app.Documents[sourceDocumentIndex];
            source.HasCompileResult = true;
            source.CompileSucceeded = false;
            source.CompileMessage = "Could not open definition: " + definition.FilePath.string();
            return;
        }
    }
    else
    {
        app.ActiveDocumentIndex = sourceDocumentIndex;
        app.PendingSelectDocumentId = app.Documents[sourceDocumentIndex].DocumentId;
    }

    EditorDocument& target = app.Documents[targetIndex];
    QueueEditorNavigation(
        target,
        std::min(definition.Range.Begin.Offset, target.Text.size()));
}

int FindDiagnosticLine(const std::string& text, const std::string& error)
{
    size_t p = 0;
    while ((p = error.find('\'', p)) != std::string::npos)
    {
        const size_t e = error.find('\'', p + 1);
        if (e == std::string::npos) break;
        std::string symbol = error.substr(p + 1, e - p - 1);
        const size_t q = symbol.rfind("::");
        if (q != std::string::npos) symbol = symbol.substr(q + 2);
        const size_t hit = text.find(symbol);
        if (hit != std::string::npos)
            return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(hit), '\n'));
        p = e + 1;
    }
    return 1;
}

void Compile(EditorAppState& app, EditorDocument& state)
{
    if (state.FilePath.empty())
    {
        state.HasCompileResult = true;
        state.CompileSucceeded = false;
        state.CompileMessage =
            "Save As once before compiling so the editor has a path for resolving relative :include files. "
            "After that, Compile/F7 never saves automatically.";
        return;
    }

    HTNCompilerDomainLoader loader;
    HTNCompilerDomainLoadResult result;
    HTNDiagnosticSink diagnostics;
    state.HasCompileResult = true;
    state.ErrorLine = -1;
    state.ErrorColumn = 1;

    // Compile the exact editor buffers, not the files currently on disk. When an include
    // is open in another tab, its in-memory text wins even if that tab is dirty.
    const HTNDomainSourceProvider SourceProvider =
        [&app](const std::filesystem::path& inFilePath, std::string& outSourceText)
        {
            const int DocumentIndex = FindOpenDocumentIndex(app, inFilePath);
            if (DocumentIndex < 0)
                return false;

            outSourceText = app.Documents[DocumentIndex].Text;
            return true;
        };

    state.CompileSucceeded = loader.LoadFromSource(
        state.FilePath.string(),
        state.Text,
        SourceProvider,
        result,
        diagnostics);
    if (state.CompileSucceeded && !diagnostics.HasErrors())
    {
        state.CompileMessage = "Compile succeeded: " + result.Domain.Id + " (" +
            std::to_string(result.SourceFiles.size()) + " linked source file(s))";
    }
    else
    {
        const HTNDiagnostic* diagnostic = diagnostics.GetFirstError();
        if (diagnostic)
        {
            state.CompileMessage = diagnostic->Message;
            state.ErrorLine = std::max(1, diagnostic->Range.Begin.Line);
            state.ErrorColumn = std::max(1, diagnostic->Range.Begin.Column);
        }
        else
        {
            state.CompileMessage = "Domain compilation failed.";
            state.ErrorLine = FindDiagnosticLine(state.Text, state.CompileMessage);
            state.ErrorColumn = 1;
        }
    }
}

bool IsWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '?' || c == '@' || c == ':';
}

ImVec4 TokenColor(const std::string& token, HTNCompilerToolingTokenKind semanticKind = HTNCompilerToolingTokenKind::None)
{
    static const std::unordered_set<std::string> keywords = {
        ":domain", ":include", ":method", ":axiom", ":constants", "top_level_domain", "top_level_method",
        "base", "overrides", "and", "or", "alt", "not", "call"
    };
    if (semanticKind == HTNCompilerToolingTokenKind::VariableInvalid ||
        semanticKind == HTNCompilerToolingTokenKind::ConstantInvalid ||
        semanticKind == HTNCompilerToolingTokenKind::MethodInvalid)
        return ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
    if (semanticKind == HTNCompilerToolingTokenKind::VariableValid) return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
    if (semanticKind == HTNCompilerToolingTokenKind::ConstantValid) return ImVec4(0.75f, 0.55f, 0.95f, 1.0f);
    if (semanticKind == HTNCompilerToolingTokenKind::MethodValid) return ImVec4(0.45f, 0.82f, 0.90f, 1.0f);
    if (keywords.contains(token)) return ImVec4(0.40f, 0.75f, 1.00f, 1.0f);
    if (!token.empty() && token[0] == '?') return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    if (!token.empty() && token[0] == '@') return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

void RenderHighlightedLine(const std::string& line)
{
    size_t i = 0;
    bool first = true;
    while (i < line.size())
    {
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/')
        {
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.45f, 1.0f), "%s", line.substr(i).c_str());
            return;
        }
        if (line[i] == '"')
        {
            size_t e = i + 1;
            while (e < line.size()) { if (line[e++] == '"') break; }
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 0.55f, 1.0f), "%s", line.substr(i, e-i).c_str());
            i = e; first = false; continue;
        }
        if (IsWordChar(line[i]))
        {
            size_t e = i + 1; while (e < line.size() && IsWordChar(line[e])) ++e;
            const std::string token = line.substr(i, e-i);
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(TokenColor(token), "%s", token.c_str());
            i = e; first = false; continue;
        }
        size_t e = i + 1; while (e < line.size() && !IsWordChar(line[e]) && line[e] != '"' && !(e+1 < line.size() && line[e]=='/' && line[e+1]=='/')) ++e;
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(line.substr(i, e-i).c_str());
        i = e; first = false;
    }
    if (line.empty()) ImGui::TextUnformatted(" ");
}


struct EditorTextPosition
{
    int Line = 1;
    size_t LineStart = 0;
    size_t OffsetInLine = 0;
};

EditorTextPosition GetEditorTextPosition(const std::string& text, size_t offset)
{
    EditorTextPosition result;
    offset = std::min(offset, text.size());

    for (size_t i = 0; i < offset; ++i)
    {
        if (text[i] == '\n')
        {
            ++result.Line;
            result.LineStart = i + 1;
        }
    }

    result.OffsetInLine = offset - result.LineStart;
    return result;
}

size_t GetLineStartOffset(const std::string& text, size_t cursorOffset)
{
    cursorOffset = std::min(cursorOffset, text.size());
    if (cursorOffset == 0) return 0;
    const size_t newline = text.rfind('\n', cursorOffset - 1);
    return newline == std::string::npos ? 0 : newline + 1;
}

size_t GetLineEndOffset(const std::string& text, size_t cursorOffset)
{
    cursorOffset = std::min(cursorOffset, text.size());
    const size_t newline = text.find('\n', cursorOffset);
    return newline == std::string::npos ? text.size() : newline;
}

size_t MoveEditorOffsetByLines(
    const std::string& text,
    size_t cursorOffset,
    int lineDelta)
{
    cursorOffset = std::min(cursorOffset, text.size());

    const size_t originalLineStart = GetLineStartOffset(text, cursorOffset);
    const size_t desiredColumn = cursorOffset - originalLineStart;
    size_t targetLineStart = originalLineStart;

    if (lineDelta < 0)
    {
        for (int step = 0; step < -lineDelta; ++step)
        {
            if (targetLineStart == 0)
                break;

            targetLineStart = GetLineStartOffset(text, targetLineStart - 1);
        }
    }
    else
    {
        for (int step = 0; step < lineDelta; ++step)
        {
            const size_t lineEnd = GetLineEndOffset(text, targetLineStart);
            if (lineEnd >= text.size())
                break;

            targetLineStart = lineEnd + 1;
        }
    }

    const size_t targetLineEnd = GetLineEndOffset(text, targetLineStart);
    return std::min(targetLineStart + desiredColumn, targetLineEnd);
}

float MeasureEditorTextWidth(const std::string& text, size_t lineStart, size_t offsetInLine)
{
    if (offsetInLine == 0 || lineStart >= text.size())
        return 0.0f;

    const size_t count = std::min(offsetInLine, text.size() - lineStart);
    return ImGui::CalcTextSize(text.substr(lineStart, count).c_str()).x;
}

float MeasureEditorDocumentWidth(const std::string& text)
{
    float maxWidth = 0.0f;
    size_t lineStart = 0;
    while (lineStart <= text.size())
    {
        const size_t newline = text.find('\n', lineStart);
        const size_t lineEnd = newline == std::string::npos ? text.size() : newline;
        if (lineEnd > lineStart)
        {
            const std::string line = text.substr(lineStart, lineEnd - lineStart);
            maxWidth = std::max(maxWidth, ImGui::CalcTextSize(line.c_str()).x);
        }
        if (newline == std::string::npos)
            break;
        lineStart = newline + 1;
    }
    return maxWidth;
}

void EnsureEditorCaretVisible(
    const std::string& text,
    size_t cursorOffset,
    float gutter,
    float lineHeight,
    float viewportWidth,
    float viewportHeight)
{
    const EditorTextPosition pos = GetEditorTextPosition(text, cursorOffset);
    const float horizontalMargin = std::max(24.0f, ImGui::CalcTextSize("    ").x);
    const float verticalMargin = lineHeight * 2.0f;

    const float caretX = gutter + MeasureEditorTextWidth(text, pos.LineStart, pos.OffsetInLine);
    const float caretY = ImGui::GetStyle().FramePadding.y + (pos.Line - 1) * lineHeight;

    float targetScrollX = ImGui::GetScrollX();
    float targetScrollY = ImGui::GetScrollY();

    // Keep the caret inside the useful text viewport. The gutter is intentionally left
    // visible, so horizontal following starts after it.
    const float visibleTextLeft = targetScrollX + gutter;
    const float visibleTextRight = targetScrollX + std::max(gutter + 1.0f, viewportWidth) - horizontalMargin;
    if (caretX < visibleTextLeft)
        targetScrollX = std::max(0.0f, caretX - gutter);
    else if (caretX > visibleTextRight)
        targetScrollX = std::max(0.0f, caretX - viewportWidth + horizontalMargin);

    const float visibleTop = targetScrollY + verticalMargin;
    const float visibleBottom = targetScrollY + std::max(lineHeight, viewportHeight) - verticalMargin;
    if (caretY < visibleTop)
        targetScrollY = std::max(0.0f, caretY - verticalMargin);
    else if (caretY + lineHeight > visibleBottom)
        targetScrollY = std::max(0.0f, caretY + lineHeight - viewportHeight + verticalMargin);

    if (std::fabs(targetScrollX - ImGui::GetScrollX()) > 0.5f)
        ImGui::SetScrollX(targetScrollX);
    if (std::fabs(targetScrollY - ImGui::GetScrollY()) > 0.5f)
        ImGui::SetScrollY(targetScrollY);
}

void DrawEditorSelection(
    ImDrawList* draw,
    const std::string& text,
    size_t selectionStart,
    size_t selectionEnd,
    const ImVec2& origin,
    float gutter,
    float scrollX,
    float scrollY,
    float lineHeight,
    float width)
{
    if (selectionStart == selectionEnd)
        return;

    if (selectionStart > selectionEnd)
        std::swap(selectionStart, selectionEnd);

    selectionStart = std::min(selectionStart, text.size());
    selectionEnd = std::min(selectionEnd, text.size());

    const float y0 = origin.y + ImGui::GetStyle().FramePadding.y - scrollY;
    const float x0 = origin.x + gutter - scrollX;
    const ImU32 selectionColor = IM_COL32(70, 105, 160, 145);

    size_t lineStart = 0;
    int lineNo = 1;

    while (lineStart <= text.size())
    {
        const size_t newline = text.find('\n', lineStart);
        const size_t lineEnd = newline == std::string::npos ? text.size() : newline;

        if (selectionEnd >= lineStart && selectionStart <= lineEnd)
        {
            const size_t a = std::max(selectionStart, lineStart);
            const size_t b = std::min(selectionEnd, lineEnd);

            float x1 = x0 + MeasureEditorTextWidth(text, lineStart, a - lineStart);
            float x2 = x0 + MeasureEditorTextWidth(text, lineStart, b - lineStart);

            // Make a selected newline visible as a small block after the text.
            if (selectionEnd > lineEnd && b == lineEnd)
                x2 = std::max(x2, x1 + ImGui::CalcTextSize(" ").x);

            if (x2 > x1)
            {
                const float y = y0 + (lineNo - 1) * lineHeight;
                draw->AddRectFilled(
                    ImVec2(x1, y),
                    ImVec2(std::min(x2, origin.x + width), y + lineHeight),
                    selectionColor);
            }
        }

        if (newline == std::string::npos)
            break;

        lineStart = newline + 1;
        ++lineNo;
    }
}

void DrawEditorCaret(
    ImDrawList* draw,
    const std::string& text,
    size_t cursorOffset,
    const ImVec2& origin,
    float gutter,
    float scrollX,
    float scrollY,
    float lineHeight)
{
    const EditorTextPosition pos = GetEditorTextPosition(text, cursorOffset);

    const float x =
        origin.x + gutter - scrollX +
        MeasureEditorTextWidth(text, pos.LineStart, pos.OffsetInLine);

    const float y =
        origin.y + ImGui::GetStyle().FramePadding.y - scrollY +
        (pos.Line - 1) * lineHeight;

    draw->AddLine(
        ImVec2(x, y),
        ImVec2(x, y + lineHeight),
        IM_COL32(240, 240, 240, 255),
        1.0f);
}

int EditorInputTextCallback(ImGuiInputTextCallbackData* data)
{
    EditorDocument& state = *static_cast<EditorDocument*>(data->UserData);
    if (data->EventFlag != ImGuiInputTextFlags_CallbackCompletion)
        return 0;

    const std::string currentText(data->Buf, static_cast<size_t>(data->BufTextLen));
    const size_t cursor = static_cast<size_t>(std::max(data->CursorPos, 0));
    size_t prefixStart = cursor;
    while (prefixStart > 0 && IsWordChar(currentText[prefixStart - 1])) --prefixStart;
    const std::string prefix = currentText.substr(prefixStart, cursor - prefixStart);

    AnalyzeDocument(state, currentText);
    if (!prefix.empty() && prefix[0] == '?')
    {
        auto candidates = state.ToolingModel.GetAutocompleteCandidates(cursor);
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const std::string& candidate)
        {
            return candidate.rfind(prefix, 0) != 0 || candidate == prefix;
        }), candidates.end());
        if (!candidates.empty())
        {
            data->DeleteChars(static_cast<int>(prefixStart), static_cast<int>(cursor - prefixStart));
            data->InsertChars(static_cast<int>(prefixStart), candidates.front().c_str());
            return 0;
        }
    }

    // Preserve convenient indentation when there is no semantic completion to apply.
    data->InsertChars(data->CursorPos, "    ");
    return 0;
}

void RenderCodeEditor(EditorDocument& state, const ImVec2& size)
{
    ImGui::BeginChild("CodeEditor", size, true, ImGuiWindowFlags_HorizontalScrollbar);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float gutter = 54.0f;
    const int lineCount = 1 + static_cast<int>(std::count(state.Text.begin(), state.Text.end(), '\n'));
    const float contentHeight = std::max(ImGui::GetContentRegionAvail().y, lineCount * lineHeight + 12.0f);
    const float documentTextWidth = MeasureEditorDocumentWidth(state.Text);
    const float width = std::max(
        ImGui::GetContentRegionAvail().x,
        gutter + documentTextWidth + 48.0f);

    if (state.PendingGotoLine > 0)
    {
        ImGui::SetKeyboardFocusHere();
        state.PendingGotoLine = -1;
    }

    const float sx = ImGui::GetScrollX();
    const float sy = ImGui::GetScrollY();

    // GetCursorScreenPos() already includes the child scroll. Convert it once to an
    // unscrolled content origin, then subtract scroll exactly once when drawing.
    // The old code subtracted sx/sy from an already-scrolled origin, which made the
    // visible range drift away from the real viewport on large documents.
    const ImVec2 scrolledCursorScreen = ImGui::GetCursorScreenPos();
    const ImVec2 origin(
        scrolledCursorScreen.x + sx,
        scrolledCursorScreen.y + sy);

    // Use the actual child-window clip rectangle. GetWindowContentRegionMax() describes
    // the content region, not necessarily the currently visible rectangle once scrolling
    // and child decorations are involved.
    ImGuiWindow* editorWindow = ImGui::GetCurrentWindow();
    const ImVec2 viewportMin = editorWindow->InnerRect.Min;
    const ImVec2 viewportMax = editorWindow->InnerRect.Max;

    // Navigation scroll is intentionally applied on a later frame. When F11 changes
    // document tabs, the first frame for the target tab is required to establish the
    // child content size/ScrollMax. Applying the scroll before that frame completes
    // causes ImGui to clamp the request against stale bounds.
    if (state.PendingScrollToOffset.has_value())
    {
        if (state.PendingScrollDelayFrames > 0)
        {
            --state.PendingScrollDelayFrames;
        }
        else
        {
            const size_t targetOffset =
                std::min(*state.PendingScrollToOffset, state.Text.size());
            const EditorTextPosition targetPos =
                GetEditorTextPosition(state.Text, targetOffset);

            const float viewportWidth =
                std::max(1.0f, viewportMax.x - viewportMin.x);
            const float viewportHeight =
                std::max(lineHeight, viewportMax.y - viewportMin.y);

            const float targetX =
                gutter +
                MeasureEditorTextWidth(
                    state.Text,
                    targetPos.LineStart,
                    targetPos.OffsetInLine);

            const float targetY =
                ImGui::GetStyle().FramePadding.y +
                (targetPos.Line - 1) * lineHeight;

            const float desiredScrollY =
                std::clamp(
                    targetY - viewportHeight * 0.45f,
                    0.0f,
                    editorWindow->ScrollMax.y);

            float desiredScrollX = editorWindow->Scroll.x;
            const float horizontalMargin = 48.0f;
            const float visibleTextLeft = desiredScrollX + gutter;
            const float visibleTextRight =
                desiredScrollX + viewportWidth - horizontalMargin;

            if (targetX < visibleTextLeft)
                desiredScrollX = std::max(0.0f, targetX - gutter);
            else if (targetX > visibleTextRight)
                desiredScrollX =
                    std::max(0.0f, targetX - viewportWidth + horizontalMargin);

            desiredScrollX =
                std::clamp(desiredScrollX, 0.0f, editorWindow->ScrollMax.x);

            // Internal overloads target the exact child window, avoiding ambiguity with
            // InputTextMultiline's own internals.
            ImGui::SetScrollY(editorWindow, desiredScrollY);
            ImGui::SetScrollX(editorWindow, desiredScrollX);

            state.PendingScrollToOffset.reset();
            state.PendingScrollDelayFrames = 0;
        }
    }

    const float y0 = origin.y + ImGui::GetStyle().FramePadding.y - sy;
    const float x0 = origin.x + gutter - sx;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Walk the editor buffer directly. This keeps render offsets identical to semantic
    // offsets and avoids a second stream representation of large documents.
    size_t lineStart = 0;
    int lineNo = 1;
    while (lineStart <= state.Text.size())
    {
        const size_t newline = state.Text.find('\n', lineStart);
        const size_t lineEnd = newline == std::string::npos ? state.Text.size() : newline;
        const std::string_view line(state.Text.data() + lineStart, lineEnd - lineStart);

        const float y = y0 + (lineNo - 1) * lineHeight;
        const bool visible =
            y + lineHeight >= viewportMin.y &&
            y <= viewportMax.y;

        if (visible)
        {
            if (lineNo == state.ErrorLine)
            {
                draw->AddRectFilled(
                    ImVec2(viewportMin.x, y),
                    ImVec2(viewportMax.x, y + lineHeight),
                    IM_COL32(115,28,34,125));
            }

            char number[16];
            snprintf(number, sizeof(number), "%4d", lineNo);
            draw->AddText(
                ImVec2(viewportMin.x + 7.0f, y),
                IM_COL32(115,120,130,255),
                number);

            size_t i = 0;
            float x = x0;
            while (i < line.size())
            {
                size_t e = i;
                ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

                if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/')
                {
                    e = line.size();
                    color = IM_COL32(110,175,110,255);
                }
                else if (line[i] == '"')
                {
                    e = i + 1;
                    while (e < line.size())
                    {
                        if (line[e++] == '"')
                            break;
                    }
                    color = IM_COL32(165,215,140,255);
                }
                else if (IsWordChar(line[i]))
                {
                    e = i + 1;
                    while (e < line.size() && IsWordChar(line[e]))
                        ++e;

                    const std::string token(line.substr(i, e - i));
                    color = ImGui::ColorConvertFloat4ToU32(TokenColor(
                        token,
                        state.ToolingModel.GetTokenKind(lineStart + i)));
                }
                else
                {
                    e = i + 1;
                    while (e < line.size() &&
                           !IsWordChar(line[e]) &&
                           line[e] != '"' &&
                           !(e + 1 < line.size() && line[e] == '/' && line[e + 1] == '/'))
                    {
                        ++e;
                    }
                }

                const std::string piece(line.substr(i, e - i));
                draw->AddText(ImVec2(x, y), color, piece.c_str());
                x += ImGui::CalcTextSize(piece.c_str()).x;
                i = e;
            }
        }

        if (newline == std::string::npos)
            break;

        lineStart = newline + 1;
        ++lineNo;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(gutter, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0,0,0,0));

    const ImGuiID editorId = ImGui::GetID("##source");

    // ImGui's multiline widget is the editing engine, but we explicitly provide editor-style
    // Home/End semantics so they always operate on the current logical line. Capture the
    // cursor before InputText processes the key, then override its final caret position.
    const bool editorWasActive = ImGui::GetActiveID() == editorId;
    const bool gotoDefinitionPressed = editorWasActive &&
        (state.PendingGotoDefinitionKey || ImGui::IsKeyPressed(ImGuiKey_F11, false));
    int gotoDefinitionCursorBeforeInput = -1;
    if (gotoDefinitionPressed)
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
            gotoDefinitionCursorBeforeInput = inputState->Stb.cursor;
    }
    // SDL keypad Home/End may arrive as KP_7/KP_1 instead of ImGuiKey_Home/End
    // (notably when NumLock is disabled). The event loop records those raw keys in
    // EditorDocument. Keep the ImGui checks as the normal-keyboard path/fallback.
    const bool homePressed = editorWasActive &&
        (state.PendingLineHome || ImGui::IsKeyPressed(ImGuiKey_Home, false));
    const bool endPressed = editorWasActive &&
        (state.PendingLineEnd || ImGui::IsKeyPressed(ImGuiKey_End, false));
    const bool extendLineSelection =
        (state.PendingLineHome || state.PendingLineEnd)
            ? state.PendingLineNavigationExtendSelection
            : ImGui::GetIO().KeyShift;

    const bool pageUpPressed = editorWasActive &&
        (state.PendingPageUp || ImGui::IsKeyPressed(ImGuiKey_PageUp, false));
    const bool pageDownPressed = editorWasActive &&
        (state.PendingPageDown || ImGui::IsKeyPressed(ImGuiKey_PageDown, false));
    const bool extendPageSelection =
        (state.PendingPageUp || state.PendingPageDown)
            ? state.PendingPageNavigationExtendSelection
            : ImGui::GetIO().KeyShift;

    int lineNavigationCursorBeforeInput = -1;
    int lineNavigationSelectionStartBeforeInput = -1;
    int lineNavigationSelectionEndBeforeInput = -1;
    if ((homePressed || endPressed))
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
        {
            lineNavigationCursorBeforeInput = inputState->Stb.cursor;
            lineNavigationSelectionStartBeforeInput = inputState->Stb.select_start;
            lineNavigationSelectionEndBeforeInput = inputState->Stb.select_end;
        }
    }

    int pageNavigationCursorBeforeInput = -1;
    int pageNavigationSelectionStartBeforeInput = -1;
    int pageNavigationSelectionEndBeforeInput = -1;
    if (pageUpPressed || pageDownPressed)
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
        {
            pageNavigationCursorBeforeInput = inputState->Stb.cursor;
            pageNavigationSelectionStartBeforeInput = inputState->Stb.select_start;
            pageNavigationSelectionEndBeforeInput = inputState->Stb.select_end;
        }
    }

    const bool changed = ImGui::InputTextMultiline(
        "##source",
        &state.Text,
        ImVec2(width, contentHeight),
        ImGuiInputTextFlags_CallbackCompletion,
        EditorInputTextCallback,
        &state);

    const bool editorActive = ImGui::IsItemActive();

    if (gotoDefinitionPressed && gotoDefinitionCursorBeforeInput >= 0)
        state.PendingDefinitionLookupOffset = static_cast<size_t>(std::max(gotoDefinitionCursorBeforeInput, 0));
    state.PendingGotoDefinitionKey = false;

    if (state.PendingGotoOffset.has_value())
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
        {
            const int targetCursor = static_cast<int>(std::min(*state.PendingGotoOffset, state.Text.size()));
            inputState->Stb.cursor = targetCursor;

            if (state.PendingSelectionStart.has_value() && state.PendingSelectionEnd.has_value())
            {
                inputState->Stb.select_start = static_cast<int>(
                    std::min(*state.PendingSelectionStart, state.Text.size()));
                inputState->Stb.select_end = static_cast<int>(
                    std::min(*state.PendingSelectionEnd, state.Text.size()));
            }
            else
            {
                inputState->Stb.select_start = targetCursor;
                inputState->Stb.select_end = targetCursor;
            }

            inputState->CursorFollow = true;
            inputState->CursorAnimReset();
            state.PendingGotoOffset.reset();
            state.PendingSelectionStart.reset();
            state.PendingSelectionEnd.reset();
        }
    }


    if ((homePressed || endPressed) && lineNavigationCursorBeforeInput >= 0)
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
        {
            const size_t originalCursor = static_cast<size_t>(std::max(lineNavigationCursorBeforeInput, 0));
            const size_t target = homePressed
                ? GetLineStartOffset(state.Text, originalCursor)
                : GetLineEndOffset(state.Text, originalCursor);
            const int targetCursor = static_cast<int>(std::min(target, state.Text.size()));

            if (extendLineSelection)
            {
                const bool hadSelection = lineNavigationSelectionStartBeforeInput != lineNavigationSelectionEndBeforeInput;
                if (!hadSelection)
                    inputState->Stb.select_start = lineNavigationCursorBeforeInput;
                else
                    inputState->Stb.select_start = lineNavigationSelectionStartBeforeInput;
                inputState->Stb.select_end = targetCursor;
            }
            else
            {
                inputState->Stb.select_start = targetCursor;
                inputState->Stb.select_end = targetCursor;
            }
            inputState->Stb.cursor = targetCursor;
            inputState->CursorFollow = true;
            inputState->CursorAnimReset();
            state.CursorOffset = static_cast<size_t>(targetCursor);
        }
    }

    if ((pageUpPressed || pageDownPressed) && pageNavigationCursorBeforeInput >= 0)
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
        {
            // One page is the visible editor height, minus one line so the old and new
            // pages retain a small amount of visual context like a normal text editor.
            const float viewportHeight = std::max(lineHeight, viewportMax.y - viewportMin.y);
            const int visibleLines = std::max(1, static_cast<int>(std::floor(viewportHeight / lineHeight)));
            const int pageLineDelta = std::max(1, visibleLines - 1) * (pageUpPressed ? -1 : 1);

            const size_t originalCursor = static_cast<size_t>(std::max(pageNavigationCursorBeforeInput, 0));
            const size_t target = MoveEditorOffsetByLines(state.Text, originalCursor, pageLineDelta);
            const int targetCursor = static_cast<int>(std::min(target, state.Text.size()));

            if (extendPageSelection)
            {
                const bool hadSelection =
                    pageNavigationSelectionStartBeforeInput != pageNavigationSelectionEndBeforeInput;
                if (!hadSelection)
                    inputState->Stb.select_start = pageNavigationCursorBeforeInput;
                else
                    inputState->Stb.select_start = pageNavigationSelectionStartBeforeInput;
                inputState->Stb.select_end = targetCursor;
            }
            else
            {
                inputState->Stb.select_start = targetCursor;
                inputState->Stb.select_end = targetCursor;
            }

            inputState->Stb.cursor = targetCursor;
            inputState->CursorFollow = true;
            inputState->CursorAnimReset();
            state.CursorOffset = static_cast<size_t>(targetCursor);
        }
    }

    // Raw SDL navigation is a one-frame event. Consume it even if the editor was not
    // active so an old keypad press can never affect a later focus change.
    state.PendingLineHome = false;
    state.PendingLineEnd = false;
    state.PendingLineNavigationExtendSelection = false;
    state.PendingPageUp = false;
    state.PendingPageDown = false;
    state.PendingPageNavigationExtendSelection = false;

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (changed)
    {
        state.Dirty = true;
        state.HasCompileResult = false;
        state.ErrorLine = -1;
        state.ErrorColumn = 1;
        AnalyzeDocument(state, state.Text);
    }

    // InputText is our editing engine, but its visuals are transparent.
    // Recreate selection and caret explicitly on top of the highlighted text.
    if (editorActive)
    {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(editorId))
        {
            const size_t cursor = static_cast<size_t>(std::max(inputState->Stb.cursor, 0));
            const size_t selectionStart = static_cast<size_t>(std::max(inputState->Stb.select_start, 0));
            const size_t selectionEnd = static_cast<size_t>(std::max(inputState->Stb.select_end, 0));

            const bool caretMoved =
                state.ForceCaretFollow ||
                cursor != state.CursorOffset ||
                changed ||
                homePressed ||
                endPressed ||
                pageUpPressed ||
                pageDownPressed;

            if (caretMoved && !state.PendingScrollToOffset.has_value())
            {
                EnsureEditorCaretVisible(
                    state.Text,
                    cursor,
                    gutter,
                    lineHeight,
                    viewportMax.x - viewportMin.x,
                    viewportMax.y - viewportMin.y);
            }

            state.CursorOffset = cursor;
            state.ForceCaretFollow = false;

            // Variable autocomplete is semantic and scope-aware. Show matches at the caret and
            // let Tab replace the current ?prefix with the first candidate.
            size_t prefixStart = cursor;
            while (prefixStart > 0 && IsWordChar(state.Text[prefixStart - 1])) --prefixStart;
            const std::string prefix = state.Text.substr(prefixStart, cursor - prefixStart);
            if (!prefix.empty() && prefix[0] == '?')
            {
                auto candidates = state.ToolingModel.GetAutocompleteCandidates(cursor);
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const std::string& candidate)
                {
                    return candidate.rfind(prefix, 0) != 0 || candidate == prefix;
                }), candidates.end());

                if (!candidates.empty())
                {
                    const EditorTextPosition caretPos = GetEditorTextPosition(state.Text, cursor);
                    const float popupX = origin.x + gutter - sx + MeasureEditorTextWidth(state.Text, caretPos.LineStart, caretPos.OffsetInLine);
                    const float popupY = origin.y + ImGui::GetStyle().FramePadding.y - sy + caretPos.Line * lineHeight;
                    ImGui::SetNextWindowPos(ImVec2(popupX, popupY), ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.97f);
                    if (ImGui::Begin("##htn_autocomplete", nullptr,
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
                    {
                        const size_t count = std::min<size_t>(candidates.size(), 8u);
                        for (size_t candidateIndex = 0; candidateIndex < count; ++candidateIndex)
                        {
                            if (candidateIndex == 0) ImGui::TextColored(ImVec4(0.95f,0.75f,0.35f,1.0f), "%s", candidates[candidateIndex].c_str());
                            else ImGui::TextUnformatted(candidates[candidateIndex].c_str());
                        }
                        ImGui::TextDisabled("Tab to complete");
                    }
                    ImGui::End();

                }
            }

            DrawEditorSelection(
                draw, state.Text, selectionStart, selectionEnd,
                origin, gutter, sx, sy, lineHeight, width);

            const bool caretVisible =
                inputState->CursorAnim <= 0.0f ||
                std::fmod(inputState->CursorAnim, 1.20f) <= 0.80f;

            if (caretVisible)
            {
                DrawEditorCaret(
                    draw, state.Text, cursor,
                    origin, gutter, sx, sy, lineHeight);
            }
        }
    }

    ImGui::EndChild();
}

}

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("HTN Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1440, 900, flags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) return -2;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    EditorAppState app;
    CreateNewDocument(app);

    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                app.ActiveDocumentIndex >= 0 &&
                app.ActiveDocumentIndex < static_cast<int>(app.Documents.size()))
            {
                EditorDocument& activeDocument = app.Documents[app.ActiveDocumentIndex];
                const SDL_Keycode Key = event.key.keysym.sym;
                const SDL_Scancode Scan = event.key.keysym.scancode;
                const bool IsHome = Key == SDLK_HOME || Key == SDLK_KP_7 ||
                                    Scan == SDL_SCANCODE_HOME || Scan == SDL_SCANCODE_KP_7;
                const bool IsEnd = Key == SDLK_END || Key == SDLK_KP_1 ||
                                   Scan == SDL_SCANCODE_END || Scan == SDL_SCANCODE_KP_1;
                if (IsHome || IsEnd)
                {
                    activeDocument.PendingLineHome = IsHome;
                    activeDocument.PendingLineEnd = IsEnd;
                    activeDocument.PendingLineNavigationExtendSelection =
                        (event.key.keysym.mod & KMOD_SHIFT) != 0;
                }

                const bool IsPageUp = Key == SDLK_PAGEUP || Key == SDLK_KP_9 ||
                                      Scan == SDL_SCANCODE_PAGEUP || Scan == SDL_SCANCODE_KP_9;
                const bool IsPageDown = Key == SDLK_PAGEDOWN || Key == SDLK_KP_3 ||
                                        Scan == SDL_SCANCODE_PAGEDOWN || Scan == SDL_SCANCODE_KP_3;
                if (IsPageUp || IsPageDown)
                {
                    activeDocument.PendingPageUp = IsPageUp;
                    activeDocument.PendingPageDown = IsPageDown;
                    activeDocument.PendingPageNavigationExtendSelection =
                        (event.key.keysym.mod & KMOD_SHIFT) != 0;
                }

                const bool ControlDown = (event.key.keysym.mod & KMOD_CTRL) != 0;
                if (ControlDown && Key == SDLK_s)
                    activeDocument.PendingSaveShortcut = true;
                if (ControlDown && Key == SDLK_f)
                    activeDocument.PendingFindShortcut = true;

                if (Key == SDLK_F3 || Scan == SDL_SCANCODE_F3)
                    activeDocument.PendingClipboardFindNext = true;

                // Ctrl+C/Ctrl+V intentionally continue through ImGui_ImplSDL2_ProcessEvent:
                // InputTextMultiline owns its selection/edit buffer and already implements
                // standard clipboard copy/paste semantics.

                // Editor shortcuts use both SDL keycode and physical scancode paths so
                // they do not depend on the backend's ImGui key translation.
                if (Key == SDLK_F7 || Scan == SDL_SCANCODE_F7)
                    activeDocument.PendingCompileShortcut = true;

                if (Key == SDLK_F11 || Scan == SDL_SCANCODE_F11)
                    activeDocument.PendingGotoDefinitionKey = true;
            }

            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
            {
                done = true;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("HTN Editor", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings);

        if (ImGui::Button("New"))
            CreateNewDocument(app);

        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
#ifdef _WIN32
            const auto path = ShowFileDialog(false);
            if (!path.empty() && OpenDocument(app, path) < 0)
            {
                if (app.ActiveDocumentIndex >= 0 &&
                    app.ActiveDocumentIndex < static_cast<int>(app.Documents.size()))
                {
                    EditorDocument& activeDocument = app.Documents[app.ActiveDocumentIndex];
                    activeDocument.HasCompileResult = true;
                    activeDocument.CompileSucceeded = false;
                    activeDocument.CompileMessage = "Could not open: " + path.string();
                }
            }
#endif
        }

        EditorDocument* activeDocument =
            app.ActiveDocumentIndex >= 0 &&
            app.ActiveDocumentIndex < static_cast<int>(app.Documents.size())
                ? &app.Documents[app.ActiveDocumentIndex]
                : nullptr;

        if (activeDocument && activeDocument->PendingSaveShortcut)
        {
            activeDocument->PendingSaveShortcut = false;
            Save(*activeDocument, false);
        }

        if (activeDocument && activeDocument->PendingFindShortcut)
        {
            activeDocument->PendingFindShortcut = false;
            activeDocument->FindBarOpen = true;
            activeDocument->PendingFindFocus = true;
            activeDocument->LastSearchQuery.clear();
            activeDocument->LastSearchEnd = activeDocument->CursorOffset;
        }

        if (activeDocument && activeDocument->PendingClipboardFindNext)
        {
            activeDocument->PendingClipboardFindNext = false;
            const char* clipboardText = ImGui::GetClipboardText();
            if (clipboardText != nullptr && clipboardText[0] != '\0')
                FindNextInDocument(*activeDocument, clipboardText);
        }

        // F7 compiles the currently active domain from its in-memory buffer. Includes
        // that are open in other tabs are compiled from their in-memory buffers as well.
        if (activeDocument && activeDocument->PendingCompileShortcut)
        {
            activeDocument->PendingCompileShortcut = false;
            Compile(app, *activeDocument);
        }

        ImGui::SameLine();
        if (ImGui::Button("Save") && activeDocument)
            Save(*activeDocument, false);

        ImGui::SameLine();
        if (ImGui::Button("Save As") && activeDocument)
            Save(*activeDocument, true);

        ImGui::SameLine();
        if (ImGui::Button("Compile") && activeDocument)
            Compile(app, *activeDocument);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compile active domain from memory (F7)");

        ImGui::SameLine();
        if (ImGui::Button("Generate Code"))
        {
            const std::filesystem::path SuggestedDomain =
                activeDocument != nullptr ? activeDocument->FilePath : std::filesystem::path{};
            OpenHTNCodeGenerationPanel(app.CodeGenerationPanel, SuggestedDomain);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Generate C code using the HTNTranslator options.");

        // Document tabs are deliberately rendered below the toolbar. Every domain owns its
        // own text buffer, semantic analyzer, diagnostics, cursor and ImGui editor state.
        if (ImGui::BeginTabBar("##domain_documents",
            ImGuiTabBarFlags_Reorderable |
            ImGuiTabBarFlags_AutoSelectNewTabs |
            ImGuiTabBarFlags_FittingPolicyScroll))
        {
            for (size_t documentIndex = 0; documentIndex < app.Documents.size(); ++documentIndex)
            {
                EditorDocument& document = app.Documents[documentIndex];

                std::string visibleName = document.FilePath.empty()
                    ? "Untitled.domain"
                    : document.FilePath.filename().string();
                if (document.Dirty)
                    visibleName += " *";

                const std::string tabLabel =
                    visibleName + "###document_" + std::to_string(document.DocumentId);

                ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
                if (app.PendingSelectDocumentId == document.DocumentId)
                    tabFlags |= ImGuiTabItemFlags_SetSelected;

                if (ImGui::BeginTabItem(tabLabel.c_str(), nullptr, tabFlags))
                {
                    app.ActiveDocumentIndex = static_cast<int>(documentIndex);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        app.PendingSelectDocumentId = -1;

        activeDocument =
            app.ActiveDocumentIndex >= 0 &&
            app.ActiveDocumentIndex < static_cast<int>(app.Documents.size())
                ? &app.Documents[app.ActiveDocumentIndex]
                : nullptr;

        if (activeDocument && activeDocument->FindBarOpen)
        {
            ImGui::PushID(activeDocument->DocumentId);
            ImGui::SetNextItemWidth(std::min(520.0f, ImGui::GetContentRegionAvail().x - 150.0f));
            if (activeDocument->PendingFindFocus)
            {
                ImGui::SetKeyboardFocusHere();
                activeDocument->PendingFindFocus = false;
            }

            const bool findSubmitted = ImGui::InputText(
                "##find_text",
                &activeDocument->FindText,
                ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::SameLine();
            const bool findNextClicked = ImGui::Button("Find Next");
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                activeDocument->FindBarOpen = false;

            if ((findSubmitted || findNextClicked) && !activeDocument->FindText.empty())
                FindNextInDocument(*activeDocument, activeDocument->FindText);

            ImGui::PopID();
        }

        if (activeDocument)
        {
            const float statusHeight = 145.0f;
            const float available = ImGui::GetContentRegionAvail().y - statusHeight;

            // InputTextMultiline and its child window need stable per-document ImGui IDs;
            // otherwise switching tabs would reuse the previous document's caret/selection.
            ImGui::PushID(activeDocument->DocumentId);
            RenderCodeEditor(*activeDocument, ImVec2(0, available));
            ImGui::PopID();

            if (activeDocument->PendingDefinitionLookupOffset.has_value())
            {
                const size_t lookupOffset = *activeDocument->PendingDefinitionLookupOffset;
                activeDocument->PendingDefinitionLookupOffset.reset();

                // GoToDefinition can append a new document and reallocate app.Documents,
                // therefore do not keep activeDocument references across this call.
                const int sourceDocumentIndex = app.ActiveDocumentIndex;
                GoToDefinition(app, sourceDocumentIndex, lookupOffset);
                activeDocument = nullptr;
            }

            ImGui::Separator();

            activeDocument =
                app.ActiveDocumentIndex >= 0 &&
                app.ActiveDocumentIndex < static_cast<int>(app.Documents.size())
                    ? &app.Documents[app.ActiveDocumentIndex]
                    : nullptr;

            if (activeDocument)
            {
                ImGui::TextDisabled("Compiler");
                if (!activeDocument->HasCompileResult)
                {
                    ImGui::TextDisabled(
                        "Press Compile to parse, resolve includes/overrides and validate the domain.");
                }
                else if (activeDocument->CompileSucceeded)
                {
                    ImGui::TextColored(
                        ImVec4(0.35f, 0.90f, 0.45f, 1.0f),
                        "%s",
                        activeDocument->CompileMessage.c_str());
                }
                else
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImVec4(1.00f,0.35f,0.35f,1.0f));

                    const std::string location =
                        (activeDocument->FilePath.empty()
                            ? "Untitled.domain"
                            : activeDocument->FilePath.filename().string()) +
                        ":" + std::to_string(std::max(activeDocument->ErrorLine,1)) +
                        ":" + std::to_string(std::max(activeDocument->ErrorColumn,1)) +
                        "  " + activeDocument->CompileMessage;

                    if (ImGui::Selectable(location.c_str()))
                    {
                        const int targetLine = std::max(activeDocument->ErrorLine, 1);
                        size_t targetOffset = 0;
                        for (int line = 1; line < targetLine && targetOffset < activeDocument->Text.size(); ++line)
                        {
                            const size_t newline = activeDocument->Text.find('\n', targetOffset);
                            if (newline == std::string::npos)
                                break;
                            targetOffset = newline + 1;
                        }
                        QueueEditorNavigation(*activeDocument, targetOffset);
                    }

                    ImGui::PopStyleColor();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled(
                    "Shortcuts:  Ctrl+S Save   |   Ctrl+C/V Copy/Paste   |   Ctrl+F Find   |   "
                    "F3 Find Clipboard Next   |   F7 Compile   |   F11 Go To Definition   |   "
                    "Home/End Line Start/End   |   PageUp/PageDown Move Page   |   "
                    "Shift+Navigation Select   |   Tab Autocomplete / Indent");
            }
        }
        else
        {
            ImGui::TextDisabled("No domain is open.");
        }

        ImGui::End();
        RenderHTNCodeGenerationPanel(app.CodeGenerationPanel);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
