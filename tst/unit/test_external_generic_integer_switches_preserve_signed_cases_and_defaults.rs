//@ aux-build: classified_metadata_values.rs
//@ compile-variant: -O

fn main() {
    for (input, expected) in [(-37, 15), (-5, 19), (19, 25), (41, 31), (0, 33)] {
        assert_eq!(classified_metadata_values::integer::<u16>(std::hint::black_box(input)), expected);
    }
}
