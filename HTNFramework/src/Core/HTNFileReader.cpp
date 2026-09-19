// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNFileReader.h"

#include "HTNLog.h"

#include <sstream>

HTNFileReader::HTNFileReader(const std::string& inFilePath)
{
    mFile.open(inFilePath);
}

HTNFileReader::~HTNFileReader()
{
    if (!mFile.is_open())
    {
        return;
    }

    mFile.close();
}

bool HTNFileReader::ReadFile(std::string& outFileText) const
{
    if (!mFile.is_open())
    {
        HTN_LOG_ERROR("File is not open");
        return false;
    }

    std::ostringstream Buffer;
    Buffer << mFile.rdbuf();
    outFileText = Buffer.str();

    return true;
}
