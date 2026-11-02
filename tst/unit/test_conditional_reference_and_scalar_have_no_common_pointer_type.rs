//@ compile-fail: Type mismatch
fn main() {
    let first = std::hint::black_box(false);
    let selected = if first { &17u8 } else { 23u8 };
    let _ = selected;
}
