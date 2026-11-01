macro_rules! pick {
    (; done) => { 17u16 };
    ($( $($name:ident)? ),* ; done) => { 23u16 };
}
fn main() { assert_eq!(pick!(; done), 17); }
