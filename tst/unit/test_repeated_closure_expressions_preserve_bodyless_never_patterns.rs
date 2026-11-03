#![feature(never_patterns)]

enum Void {}
macro_rules! pair { ($value:expr) => { ($value, $value) }; }

fn main() {
    let closures = pair!(|value: Void| -> u32 { match value { ! } });
    assert_eq!(std::mem::size_of_val(&closures.0), 0);
    assert_eq!(std::mem::size_of_val(&closures.1), 0);
}
