//@ aux-build: classified_metadata_values.rs
//@ compile-variant: -O

const VALUES: [usize; 5] = [
    classified_metadata_values::constant(2),
    classified_metadata_values::constant(7),
    classified_metadata_values::constant(13),
    classified_metadata_values::constant(23),
    classified_metadata_values::constant(0),
];

fn main() { assert_eq!(VALUES, [37, 41, 43, 47, 53]); }
