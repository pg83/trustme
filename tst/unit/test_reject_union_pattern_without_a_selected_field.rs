//@ compile-fail: union patterns must select exactly one field
union Choice { first: u16, second: u16 }
fn main() { let Choice {} = Choice { first: 29 }; }
