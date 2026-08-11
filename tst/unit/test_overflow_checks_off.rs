//@ compile-flags: -Coverflow-checks=off

fn main() {
    let value = std::hint::black_box(u8::MAX);
    let one = std::hint::black_box(1u8);
    assert_eq!(value + one, 0);
}
