//@ aux-build: early_header_values.rs
extern crate early_header_values;
use early_header_values::{MakeSeed, Mark};

fn obtain<T: MakeSeed>() -> T {
    T::ITEM
}

fn main() {
    let selected = if std::hint::black_box(true) {
        obtain()
    } else {
        Mark(59)
    };
    assert_eq!(selected.0, 43);

    let fallback = if std::hint::black_box(false) {
        obtain()
    } else {
        Mark(61)
    };
    assert_eq!(fallback.0, 61);
}
