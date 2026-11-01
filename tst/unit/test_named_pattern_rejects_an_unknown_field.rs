//@ compile-fail: doesn't have a field
struct Entry { tag: u8 }
fn main() { let Entry { unknown, .. } = Entry { tag: 7 }; let _ = unknown; }
