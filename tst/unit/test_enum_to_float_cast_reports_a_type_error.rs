//@ compile-fail: cast
enum Tag { First, Second }
fn main() { let _ = Tag::Second as f64; }
