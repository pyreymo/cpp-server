#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <sstream>

inline void log(const std::string &message, const char *file, int line)
{
    std::ostringstream oss;
    oss << "[" << file << ":" << line << "] " << message;
    std::cout << oss.str() << std::endl;
}

#endif // LOG_H