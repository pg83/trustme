//@ compile-variant: -O
macro_rules! text { ($ty:ty) => { stringify!($ty) }; }
fn main() {
    let printed = text!(for<'a> fn(&'a u8) -> &'a u8);
    assert!(printed.contains("for"));
    assert!(printed.contains("'a"));
    assert_eq!(printed.matches("u8").count(), 2);
}
