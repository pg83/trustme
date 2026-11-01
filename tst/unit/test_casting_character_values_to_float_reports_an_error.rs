//@ compile-fail: cast
const VALUE: f64 = 'x' as f64;
fn main() { std::hint::black_box(VALUE); }
