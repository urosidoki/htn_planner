// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "WorldState/HTNWorldState.h"
#include "WorldState/Parser/HTNWorldStateLexer.h"
#include "WorldState/Parser/HTNWorldStateParser.h"

#include <string>

class HTNDatabaseHook
{
public:
    // Parses a world state file, building a world state
    bool ParseWorldStateFile(const std::string& inWorldStateFilePath);

    // Returns the world state
    HTN_NODISCARD const HTNWorldState& GetWorldState() const;
    HTN_NODISCARD HTNWorldState& GetWorldState();

private:
    HTNWorldState mWorldState;

    const HTNWorldStateLexer  mWorldStateLexer  = HTNWorldStateLexer();
    const HTNWorldStateParser mWorldStateParser = HTNWorldStateParser();
};

inline const HTNWorldState& HTNDatabaseHook::GetWorldState() const
{
    return mWorldState;
}

inline HTNWorldState& HTNDatabaseHook::GetWorldState()
{
    return mWorldState;
}
