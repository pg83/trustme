//@ compile-variant: -O
mod builtin { pub fn value() -> u32 { 31 } }
struct Packet { value: u32 }
macro_rules! keep { ($value:expr) => { $value }; }
fn main() {
    let builtin = 13u32;
    assert_eq!(builtin * 2, 26);
    assert_eq!(builtin::value(), 31);
    assert_eq!(Packet { value: 43 }.value, 43);
    assert_eq!(keep!(59), 59);
}
