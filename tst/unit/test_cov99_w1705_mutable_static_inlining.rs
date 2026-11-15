//@ compile-flags: -Z mir-opt-level=3 -Z dump-mir

static mut SELECTOR: u8 = 0;

#[inline(never)]
fn step(mut value: u32) -> u32 {
    for _ in 0..3 {
        value = std::hint::black_box(value).wrapping_add(1);
    }
    value
}

fn selected() -> u32 {
    match unsafe { SELECTOR } {
        0 => step(7),
        1 => step(19),
        _ => step(31),
    }
}

fn main() {
    unsafe { SELECTOR = 0; }
    assert_eq!(selected(), 10);
    unsafe { SELECTOR = 1; }
    assert_eq!(selected(), 22);
    unsafe { SELECTOR = 2; }
    assert_eq!(selected(), 34);
}
