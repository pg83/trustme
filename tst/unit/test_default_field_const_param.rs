#![feature(default_field_values)]

struct Defaults<const N: usize> {
    value: usize = N + 1,
}

fn main() {
    let value: Defaults<41> = Defaults { .. };
    assert_eq!(value.value, 42);
}
