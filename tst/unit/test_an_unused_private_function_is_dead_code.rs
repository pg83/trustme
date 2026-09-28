//@ compile-fail: function `unused` is never used
// autocfg probes `#![deny(dead_code)] fn x() {}` and expects the compiler to
// refuse it. rustc's `dead_code` lint reports an item nothing reachable uses;
// under `deny` that is an error.
#![deny(dead_code)]

fn unused() -> u32 {
    1
}

fn main() {}
