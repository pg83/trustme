//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(explicit_tail_calls)]
#![allow(incomplete_features, dead_code)]

fn destination() -> bool {
    true
}

fn relay() -> bool {
    assert!(true && become destination());
    false
}

fn main() {}
