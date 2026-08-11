//@ check-pass
//@ compile-flags: -Znext-solver

use std::fmt::Debug;

fn visit_fields(names: &[&str], values: &[&dyn Debug]) -> usize {
    let mut count = 0;
    for (name, value) in std::iter::zip(names, values) {
        let _ = (name, value);
        count += 1;
    }
    count
}

fn main() {
    let first = 1u8;
    let second = 2u16;
    let names = ["first", "second"];
    let values: [&dyn Debug; 2] = [&first, &second];
    assert_eq!(visit_fields(&names, &values), 2);
}
