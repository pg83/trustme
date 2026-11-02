//@ compile-variant: -O
fn declare() {
    #[macro_export]
    macro_rules! block_exported_value { () => { 23u8 }; }
}
fn main() {
    declare();
    assert_eq!(block_exported_value!(), 23);
}
