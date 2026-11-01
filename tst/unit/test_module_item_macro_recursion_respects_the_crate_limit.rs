//@ compile-fail: Exceeded macro recursion limit
#![recursion_limit = "4"]

macro_rules! repeat { () => { repeat!(); }; }
repeat!();

fn main() {}
