// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNLspServer.h"
#include "HTNLspTransport.h"

#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main()
{
#ifdef _WIN32
    // LSP stdio is a byte protocol. Prevent Windows CRT newline translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    HTNLspTransport Transport(std::cin, std::cout);
    HTNLspServer Server(Transport);
    return Server.Run();
}
