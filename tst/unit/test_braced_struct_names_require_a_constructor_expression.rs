//@ compile-fail: Couldn't find variable name 'Record'
struct Record { value: u8 }
fn main() { let _ = Record; }
