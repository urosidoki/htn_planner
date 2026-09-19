// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "HTNUncopyable.h"

#include <fstream>
#include <string>

/**
 * Helper that uses RAII to automatically open/close a file to perform read operations on it
 */
class HTNFileReader : private HTNUncopyable
{
public:
    explicit HTNFileReader(const std::string& inFilePath);
    ~HTNFileReader();

    // Reads the text of the file
    HTN_NODISCARD bool ReadFile(std::string& outFileText) const;

    // Resets the read position to the beginning of the file
    void ResetReadPosition();

private:
    std::ifstream mFile;
};

inline void HTNFileReader::ResetReadPosition()
{
    mFile.seekg(0, std::ios::beg);
}
