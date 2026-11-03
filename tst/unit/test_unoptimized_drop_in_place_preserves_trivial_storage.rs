//@ compile-flags: -Zmir-opt-level=0

#[inline(never)]
unsafe fn discard<T>(value: &mut T) { std::ptr::drop_in_place(value); }

fn main() {
    let mut value = std::hint::black_box(73u32);
    let mut pair = std::hint::black_box((11u16, 19u16));
    unsafe { discard(&mut value); discard(&mut pair); }
    assert_eq!(value, 73);
    assert_eq!(pair, (11, 19));
}
