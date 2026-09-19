// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <istream>
#include <ostream>
#include <string>

class HTNLspTransport final
{
public:
    HTNLspTransport(std::istream& inInput, std::ostream& inOutput);

    bool ReadMessage(std::string& outJsonPayload);
    void WriteMessage(const std::string& inJsonPayload);

private:
    std::istream& mInput;
    std::ostream& mOutput;
};
