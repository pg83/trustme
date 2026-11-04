//@ compile-variant: -O
#[inline(never)]
fn transform(value: u32) -> u32 { value.rotate_left(5) ^ 0x7b31 }
#[inline(never)]
fn choose(flag: bool, value: u32) -> u32 {
    let selected;
    if flag { selected = transform; } else { selected = transform; }
    selected(value)
}
fn main() {
    for flag in [false, true] {
        assert_eq!(choose(std::hint::black_box(flag), 19), (19u32 << 5) ^ 0x7b31);
        assert_eq!(choose(std::hint::black_box(flag), 127), (127u32 << 5) ^ 0x7b31);
    }
}
