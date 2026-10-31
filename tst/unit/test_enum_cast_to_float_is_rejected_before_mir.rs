//@ compile-fail: Invalid cast from enum
enum Level { High = 37 }
fn main() { let value = Level::High; let _: f64 = value as f64; }
