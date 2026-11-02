//@ compile-fail: cast
#[repr(transparent)]
struct Word(u32);
fn main() { let _ = Word(53) as u32; }
