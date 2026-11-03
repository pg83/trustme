//@ compile-variant: -O
#![feature(extern_types)]

unsafe extern "C" { type Foreign; }

#[repr(C)]
struct Tail { tag: u16, value: Foreign }

const FOREIGN_DROP: bool = core::mem::needs_drop::<Foreign>();
const TAIL_DROP: bool = core::mem::needs_drop::<Tail>();

fn main() {
    assert_eq!(FOREIGN_DROP, TAIL_DROP);
    assert_eq!(core::mem::needs_drop::<Foreign>(), FOREIGN_DROP);
    assert!(!core::mem::needs_drop::<*mut Foreign>());
}
