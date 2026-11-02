//@ compile-fail: Type mismatch
fn main() {
    let first = std::hint::black_box(true);
    let value = 17u8;
    let selected = if first { &value } else { std::ptr::null_mut::<u8>() };
    let _ = selected;
}
