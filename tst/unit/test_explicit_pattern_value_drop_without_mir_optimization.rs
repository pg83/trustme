//@ compile-flags: -Zmir-opt-level=0
#![feature(pattern_types, pattern_type_macro)]

type Small = std::pat::pattern_type!(u8 is 1..=9);

#[inline(never)]
unsafe fn release(value: *mut Small) {
    std::ptr::drop_in_place(value);
}

fn main() {
    let mut value: Small = unsafe { std::mem::transmute(5u8) };
    unsafe { release(&mut value) };
}
