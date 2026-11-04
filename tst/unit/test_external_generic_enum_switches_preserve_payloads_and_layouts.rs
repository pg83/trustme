//@ aux-build: classified_metadata_values.rs
//@ compile-variant: -O
use classified_metadata_values::{classify, Entry};

fn main() {
    assert_eq!(classify::<u32>(Entry::Empty), 7);
    assert_eq!(classify::<u32>(Entry::First(59)), 68);
    assert_eq!(classify::<u64>(Entry::Second(61)), 76);
    assert_eq!(classify::<u64>(Entry::Last), 19);
}
