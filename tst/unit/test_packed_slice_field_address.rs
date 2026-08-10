use std::ptr::addr_of;

#[repr(C, packed(2))]
struct Sized {
    byte: u8,
    values: [u32; 1],
}

#[repr(C, packed(2))]
struct Unsized {
    byte: u8,
    values: [u32],
}

fn main() {
    let value = Sized { byte: 0, values: [1] };
    let wide: *const Unsized = unsafe { std::mem::transmute((&value, 1usize)) };
    let first = unsafe { addr_of!((*wide).values).cast::<u32>().read_unaligned() };

    assert_eq!(first, 1);
}
