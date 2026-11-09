//@ compile-fail: exactly one boolean argument
#![feature(trace_macros)]

fn main() {
    trace_macros!(false, true);
}
