//@ compile-flags: -Z dump-mir

static mut SELECTOR: u8 = 0;

#[inline(never)]
fn advance(mut value: u32) -> u32 {
    for _ in 0..3 {
        value = std::hint::black_box(value).wrapping_add(1);
    }
    value
}

fn selected() -> u32 {
    match unsafe { SELECTOR } {
        0 => advance(11),
        1 => advance(23),
        _ => advance(37),
    }
}

fn main() {
    unsafe { SELECTOR = 0; }
    assert_eq!(selected(), 14);
    unsafe { SELECTOR = 1; }
    assert_eq!(selected(), 26);
    unsafe { SELECTOR = 2; }
    assert_eq!(selected(), 40);
}
