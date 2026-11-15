#![feature(rustc_attrs)]
#![allow(internal_features)]

#[repr(transparent)]
#[rustc_layout_scalar_valid_range_start(2)]
#[rustc_layout_scalar_valid_range_end(254)]
struct Bounded(u8);

enum Packed {
    Full(Bounded),
    Empty0,
    Empty1,
    Empty2,
}

#[inline(never)]
fn decode(value: Packed) -> u16 {
    match value {
        Packed::Full(value) => value.0 as u16,
        Packed::Empty0 => 300,
        Packed::Empty1 => 301,
        Packed::Empty2 => 302,
    }
}

fn main() {
    assert_eq!(std::mem::size_of::<Packed>(), 1);
    assert_eq!(decode(Packed::Full(unsafe { Bounded(2) })), 2);
    assert_eq!(decode(Packed::Full(unsafe { Bounded(254) })), 254);
    assert_eq!(decode(Packed::Empty0), 300);
    assert_eq!(decode(Packed::Empty1), 301);
    assert_eq!(decode(Packed::Empty2), 302);
}
