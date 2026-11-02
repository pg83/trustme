//@ compile-variant: -O
macro_rules! nested {
    ($name:ident, $value:expr) => {
        mod $name {
            pub mod child {
                macro_rules! value { () => { $value }; }
                pub fn read() -> u16 { value!() }
            }
            pub fn read() -> u16 { child::read() + 1 }
        }
    };
}
nested!(first, 17);
nested!(second, 23);
fn main() {
    assert_eq!(first::read(), 18);
    assert_eq!(second::read(), 24);
}
