#![feature(default_field_values)]

struct Pair {
    first: u16 = {
        let value = 17u16;
        value + 2
    },
    second: u16 = {
        let value = 31u16;
        value + 2
    },
}

fn main() {
    let defaulted = Pair { .. };
    assert_eq!((defaulted.first, defaulted.second), (19, 33));
    let overridden = Pair { first: 41, .. };
    assert_eq!((overridden.first, overridden.second), (41, 33));
}
