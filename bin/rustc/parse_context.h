#pragma once

// Invocation-local parser state shared by the root source stream and every
// nested/module/macro stream derived from it.
class ParseContext {
    unsigned nextAnonymousItem = 0;

public:
    unsigned newAnonymousItem() {
        return nextAnonymousItem++;
    }
};
