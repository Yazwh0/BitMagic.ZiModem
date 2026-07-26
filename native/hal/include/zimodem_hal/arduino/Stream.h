#pragma once

// Matches the subset of the real Arduino core's Stream base class that zimodem actually
// uses through a `Stream*`/`Stream&` (pet2asc.ino, zprint.ino, zcommand.ino's doWebDump,
// stringstream.h): available(), read(), peek(), plus everything Print already provides.
// Real Arduino Stream also has setTimeout/find/parseInt/readBytes/readString/etc. -- none
// of those are called through a generic Stream pointer anywhere in the sketch (verified
// by grep), so they're intentionally omitted here rather than guessed at.

#include "Print.h"

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
};
