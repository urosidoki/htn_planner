// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNHotReloadDemo.h"
#include "SDL.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <cstdio>
#include <filesystem>
#include <string_view>

int main(int argc, char** argv)
{
    char* Base = SDL_GetBasePath();
    const std::filesystem::path Bin = Base ? std::filesystem::path(Base) : std::filesystem::path(".");
    SDL_free(Base);
    std::filesystem::path Root = Bin;
    std::error_code Error;
    while (!std::filesystem::is_regular_file(Root / "premake5.lua", Error))
    {
        const auto Parent = Root.parent_path();
        if (Parent.empty() || Parent == Root)
        {
            std::fprintf(stderr, "Repository root not found above executable. Run from the built bin directory.\n");
            return 1;
        }
        Root = Parent;
    }
    if (argc > 1 && (std::string_view(argv[1]) == "--self-test" ||
                    std::string_view(argv[1]) == "--pipeline-self-test"))
        return HTNHotReloadDemoSelfTest(Root, Bin, std::string_view(argv[1]) == "--pipeline-self-test");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* Window = SDL_CreateWindow("HTN Hot Reload Demo", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1440, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* Renderer = Window ? SDL_CreateRenderer(Window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!Renderer && Window)
        Renderer = SDL_CreateRenderer(Window, -1, SDL_RENDERER_SOFTWARE);
    if (!Renderer)
    {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        if (Window) SDL_DestroyWindow(Window);
        SDL_Quit();
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL2_InitForSDLRenderer(Window, Renderer);
    ImGui_ImplSDLRenderer2_Init(Renderer);
    {
        HTNHotReloadDemo Demo(Root, Bin);
        Demo.Initialize();
        bool Running = true;
        while (Running)
        {
            SDL_Event Event;
            while (SDL_PollEvent(&Event))
            {
                ImGui_ImplSDL2_ProcessEvent(&Event);
                if (Event.type == SDL_QUIT || (Event.type == SDL_WINDOWEVENT &&
                    Event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    Event.window.windowID == SDL_GetWindowID(Window)))
                    Running = false;
            }
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            Demo.Update(ImGui::GetIO().DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("HTNHotReloadDemo", nullptr, ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            Demo.Render();
            ImGui::End();
            ImGui::Render();
            SDL_SetRenderDrawColor(Renderer, 20, 20, 23, 255);
            SDL_RenderClear(Renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            SDL_RenderPresent(Renderer);
        }
    } // NPCs/units and DLLs destroyed while SDL is still available.
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);
    SDL_Quit();
    return 0;
}
