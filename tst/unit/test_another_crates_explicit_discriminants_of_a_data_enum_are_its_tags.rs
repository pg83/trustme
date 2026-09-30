//@ aux-build: explicit_discriminant_data_enum.rs
// rmp's `#[repr(u8)] enum Marker { FixPos(u8) = 0x00, FixNeg(i8) = 0xe0, .. }`:
// a variant with fields may have an explicit discriminant, and with a
// primitive repr it is the tag. zerovec's tests built `Marker::FixArray(n)`
// with the variant's index as its tag and handed it to rmp's `PartialEq`,
// which only knows the discriminants.
extern crate explicit_discriminant_data_enum;

use explicit_discriminant_data_enum::{fix_map, is_null, tag, Marker};

fn main() {
    assert!(is_null(Marker::Null));
    assert_eq!(fix_map(3), Marker::FixMap(3));
    assert_eq!(tag(&Marker::FixMap(3)), 0x80);
    assert_eq!(tag(&Marker::FixNeg(-1)), 0xe0);
    assert_eq!(tag(&Marker::False), 0xc2);
    assert_eq!(tag(&fix_map(3)), 0x80);
}
