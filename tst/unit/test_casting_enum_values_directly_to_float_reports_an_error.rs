//@ compile-fail: cast
enum Mode { First = 3, Second = 7 }
const VALUE: f64 = Mode::Second as f64;
fn main() { std::hint::black_box(VALUE); }
