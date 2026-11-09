//@ compile-fail: trace_macros! expects
#![feature(trace_macros)]

fn main() {
    trace_macros!(113);
}
