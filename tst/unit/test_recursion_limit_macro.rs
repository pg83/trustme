//@ compile-fail: recursion limit reached
// `#![recursion_limit]` bounds how deep macro expansion may nest: each expansion
// adds a frame to the span naming the macro that produced the tokens.
#![recursion_limit = "4"]
#![allow(unused)]

macro_rules! step {
    () => { step!(1); };
    (1) => { step!(2); };
    (2) => { step!(3); };
    (3) => { step!(4); };
    (4) => { };
}

fn main() {
    step! {}
}
