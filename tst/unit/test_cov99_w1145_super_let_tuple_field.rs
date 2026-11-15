//@ compile-only
#![feature(super_let)]

struct Slot(());

fn main() {
    let _slot = Slot { 0: super let _retained = 7u32 };
}
