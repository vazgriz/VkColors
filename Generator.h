#pragma once

class Generator {
public:
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual ~Generator() {}
};