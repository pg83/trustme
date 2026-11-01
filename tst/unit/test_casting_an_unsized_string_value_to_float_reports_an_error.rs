//@ compile-fail: cast
const VALUE: f32 = *"hello" as f32;
fn main() { std::hint::black_box(VALUE); }
