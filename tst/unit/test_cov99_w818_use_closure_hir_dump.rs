//@ compile-flags: -Zdump-hir
#![feature(ergonomic_clones)]

fn main() {
    let value = 818u32;
    let read = use || value;
    assert_eq!(read(), value);
}
