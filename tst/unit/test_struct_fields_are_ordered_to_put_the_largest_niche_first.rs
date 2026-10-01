// Upstream's struct layout (`univariant`) sorts fields by alignment group and
// then by the size of their niche, largest first, so the niche lands where an
// enum around the struct can use it with room for other variants; when the
// niche would still sit inside the struct it also tries the order that puts it
// last and keeps the one with the niche nearer an edge. We sorted by
// alignment only: `S` kept `a` first and `U` put its array first. A tuple
// may end in an unsized element, so its last element stays last.
#![allow(dead_code)]
use std::mem::offset_of;

struct S {
    a: u8,
    b: bool,
}

struct T {
    x: u16,
    y: u8,
    z: bool,
}

struct U {
    a: u8,
    b: [u8; 16],
    c: bool,
}

struct V {
    a: u32,
    b: u8,
    c: bool,
    d: u16,
}

#[derive(Clone, Copy)]
enum Pad {
    None,
    Zero,
    Space,
}

struct FullYear {
    padding: Pad,
    repr: Pad,
    iso: bool,
    sign: bool,
}

fn main() {
    assert_eq!((offset_of!(S, b), offset_of!(S, a)), (0, 1));
    assert_eq!((offset_of!((u8, bool, u16), 0), offset_of!((u8, bool, u16), 1), offset_of!((u8, bool, u16), 2)), (1, 0, 2));
    assert_eq!((offset_of!(T, x), offset_of!(T, y), offset_of!(T, z)), (0, 2, 3));
    assert_eq!((offset_of!(U, a), offset_of!(U, b), offset_of!(U, c)), (1, 2, 0));
    assert_eq!((offset_of!(V, a), offset_of!(V, b), offset_of!(V, c), offset_of!(V, d)), (0, 6, 7, 4));
    assert_eq!(
        (offset_of!(FullYear, padding), offset_of!(FullYear, repr), offset_of!(FullYear, iso), offset_of!(FullYear, sign)),
        (2, 3, 0, 1)
    );
}
