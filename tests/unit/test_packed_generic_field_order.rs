use std::mem;

#[repr(packed)]
struct Packed<T> {
    first: T,
    middle: u8,
    last: u32,
}

fn main() {
    let value = Packed { first: 0xffff_ffffu32, middle: 1, last: 0xaaaa_aaaa };
    let bytes: [u8; 9] = unsafe { mem::transmute(value) };

    assert_eq!(bytes, [0xff, 0xff, 0xff, 0xff, 1, 0xaa, 0xaa, 0xaa, 0xaa]);
}
