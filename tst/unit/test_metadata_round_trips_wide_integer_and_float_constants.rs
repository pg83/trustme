//@ aux-build: numeric_metadata.rs
extern crate numeric_metadata;
fn main() {
    assert_eq!(numeric_metadata::SIGNED, -(1_i128 << 100) + 17);
    assert_eq!(numeric_metadata::UNSIGNED, (1_u128 << 110) + 29);
    assert_eq!(numeric_metadata::FLOAT, -3.75);
    assert_eq!(numeric_metadata::adjust(numeric_metadata::SIGNED), -(1_i128 << 100) - 2);
}
