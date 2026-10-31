//@ compile-fail: Invalid cast from enum
enum Letter { A = 65 }
fn main() { let value = Letter::A; let _: char = value as char; }
