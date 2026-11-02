//@ compile-fail: Type mismatch
fn main() {
    let first = std::hint::black_box(true);
    let value = if first { &[17u8; 2] } else { &[23u8; 3] };
    assert_eq!(value[0], if first { 17 } else { 23 });
}
