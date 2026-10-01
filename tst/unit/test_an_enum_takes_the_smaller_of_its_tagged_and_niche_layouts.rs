// Upstream computes both layouts of an enum (`layout_of_enum`): the tagged one
// and the one that stores the discriminant in the largest niche of the largest
// variant. It takes the smaller; at equal size the one with the larger niche
// left over; then the tagged one. We took a niche whenever one fitted: `C5`'s
// `bool` carried the discriminant where upstream has a tag byte.
#![allow(dead_code)]
use std::mem::size_of;

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

enum Component {
    Day(Pad),
    Year(FullYear),
    Ignore(u16),
    End,
}

enum C5 {
    A(u32, bool),
    B(u32),
}

enum C6 {
    A(u8, u8, bool, u8),
    B(u16),
    C,
}

fn bytes<T>(value: &T) -> Vec<u8> {
    let start = value as *const T as *const u8;
    (0..size_of::<T>()).map(|i| unsafe { *start.add(i) }).collect()
}

fn main() {
    assert_eq!(size_of::<C5>(), 8);
    assert_eq!(bytes(&C5::A(9, true)), [0, 1, 0, 0, 9, 0, 0, 0]);
    assert_eq!(bytes(&C5::B(5)), [1, 0, 0, 0, 5, 0, 0, 0]);
    assert_eq!(size_of::<C6>(), 4);
    assert_eq!(bytes(&C6::B(0x0102)), [2, 0, 2, 1]);
    assert_eq!(bytes(&C6::C)[0], 3);
    assert_eq!(size_of::<Component>(), 4);
    assert_eq!(bytes(&Component::End)[0], 5);
    assert_eq!(bytes(&Component::Ignore(0x0102)), [4, 0, 2, 1]);
    assert_eq!(bytes(&None::<Component>)[0], 6);
}
