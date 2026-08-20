//@ compile-fail: trait bound
//@ crate-type: lib
// Bounds that do not mention an item's parameters are checked where the item
// is declared, even when the item is never instantiated.

struct Bad
where
    i32: Iterator;
