macro_rules! pick {
    ($( $($name:ident)? ),* ; done) => { 11u16 };
    (; other) => { 31u16 };
}
fn main() { assert_eq!(pick!(; other), 31); }
