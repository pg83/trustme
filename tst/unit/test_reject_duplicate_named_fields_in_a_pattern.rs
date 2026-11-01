//@ compile-fail: field occurs more than once in a pattern
struct Record { value: u16 }
fn main() { let Record { value: _, value: _ } = Record { value: 31 }; }
