//@ compile-variant: -O
use std::fmt::Debug;
const VALUE: &dyn Debug = &*(&17u8 as &dyn Debug);
fn main() {
    assert_eq!(std::mem::size_of_val(VALUE), 1);
    assert_eq!(format!("{:?}", VALUE), "17");
}
