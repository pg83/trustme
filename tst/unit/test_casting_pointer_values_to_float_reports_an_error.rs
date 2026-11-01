//@ compile-fail: cast
fn convert(value: *const u8) -> f32 { value as f32 }
fn main() { std::hint::black_box(convert(std::ptr::null())); }
