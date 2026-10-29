#![feature(associated_type_defaults)]
macro_rules! define_trait {
    ($($item:item)*) => {
        trait Measures { $(#[allow(dead_code)] $item)* }
    };
}
define_trait! {
    type Value = u16;
    const BASE: u16 = 13;
    fn base() -> u16 { Self::BASE }
}
struct Meter;
impl Measures for Meter {}
fn main() {
    let value: <Meter as Measures>::Value = 29;
    assert_eq!(value, 29);
    assert_eq!(Meter::base(), 13);
}
