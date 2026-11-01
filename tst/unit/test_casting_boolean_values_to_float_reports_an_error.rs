//@ compile-fail: cast
fn convert(value: bool) -> f64 { value as f64 }
fn main() { std::hint::black_box(convert(true)); }
