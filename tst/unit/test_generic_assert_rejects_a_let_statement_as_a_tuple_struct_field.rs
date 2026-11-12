//@ compile-fail: Type mismatch
#![feature(generic_assert)]

struct Flag(bool);

fn main() {
    assert!(Flag { 0: let inner = true }.0);
}
