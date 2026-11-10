//@ edition: 2021
#![allow(non_camel_case_types)]

struct gen {
    value: u8,
}

macro_rules! construct {
    ($kind:ident, $value:expr) => { $kind { value: $value } };
}

fn main() {
    let direct = gen { value: 7 };
    let expanded = construct!(gen, 11);
    assert_eq!(direct.value, 7);
    assert_eq!(expanded.value, 11);
}
