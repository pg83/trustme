//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(trace_macros)]

fn main() {
    let trace_macros!(false) = 294_u16;
}
