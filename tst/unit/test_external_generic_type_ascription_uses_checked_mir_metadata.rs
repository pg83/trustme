//@ aux-build: strict_ascription_metadata.rs
//@ compile-variant: -O
fn main() {
    let values = strict_ascription_metadata::identity([19u32, 61]);
    assert_eq!(values, [19, 61]);
    assert_eq!(strict_ascription_metadata::read(&values[1]), 61);
}
