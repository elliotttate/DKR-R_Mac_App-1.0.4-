#pragma once

#include "ultramodern/ultra64.h"

#include <array>
#include <cstdint>

namespace RT64 {
struct Application;
struct DisplayList;
struct GBI;
struct State;
}

namespace dkr::runtime {

std::uint64_t completed_f3ddkr_task_count();

class F3DDKRRT64Bridge {
public:
    F3DDKRRT64Bridge();
    ~F3DDKRRT64Bridge();

    F3DDKRRT64Bridge(const F3DDKRRT64Bridge&) = delete;
    F3DDKRRT64Bridge& operator=(const F3DDKRRT64Bridge&) = delete;

    void process(RT64::Application& application, const OSTask& task);

private:
    using Handler = void (*)(RT64::State*, RT64::DisplayList**);
    struct StateData;
    RT64::GBI* gbi_;
    StateData* data_;
    std::array<Handler, 256> original_handlers_{};

    static F3DDKRRT64Bridge* active_;

    static void Dispatch(RT64::State* state,
                         RT64::DisplayList** display_list);
    static void ApplyPresentationMarkers(RT64::State* state,
                                         RT64::DisplayList* display_list);
    static void ApplyPresentationGroup(RT64::State* state,
                                       std::uint32_t mode,
                                       std::uint16_t token,
                                       std::uint8_t variant);
    static void FinishShadowScope(RT64::State* state);
    static void AdjustSplitViewportCommand(RT64::State* state,
                                           RT64::DisplayList* command,
                                           std::uint8_t opcode);
    static void RejectTask(RT64::DisplayList** display_list);
    static void PresentationGroup(RT64::State* state,
                                  RT64::DisplayList** display_list);
    static void MoveMem(RT64::State* state, RT64::DisplayList** display_list);
    static void Matrix(RT64::State* state, RT64::DisplayList** display_list);
    static void FillRect(RT64::State* state, RT64::DisplayList** display_list);
    static void TextureOffset(RT64::State* state, RT64::DisplayList** display_list);
    static void Vertex(RT64::State* state, RT64::DisplayList** display_list);
    static void Triangle(RT64::State* state, RT64::DisplayList** display_list);
    static void DisplayListBranch(RT64::State* state,
                                  RT64::DisplayList** display_list);
    static void EndDisplayList(RT64::State* state,
                               RT64::DisplayList** display_list);
    static void CountedDisplayList(RT64::State* state, RT64::DisplayList** display_list);
    static void DMAOffsets(RT64::State* state, RT64::DisplayList** display_list);
    static void MoveWord(RT64::State* state, RT64::DisplayList** display_list);
    static void SetTextureImage(RT64::State* state, RT64::DisplayList** display_list);
    static void LoadBlock(RT64::State* state, RT64::DisplayList** display_list);

    static void RunCommands(RT64::State* state, RT64::DisplayList* commands,
                            std::uint32_t command_count);
};

} // namespace dkr::runtime
