//@ edition: 2021
#![allow(non_camel_case_types)]
//@ compile-fail: error
struct gen {
    value: u16,
}

macro_rules! construct {
    ($kind:path, $value:expr) => {
        $kind { value: $value }
    };
}

fn main() {
    let short = construct!(gen, 17);
    let qualified = construct!(crate::gen, 23);
    assert_eq!(short.value, 17);
    assert_eq!(qualified.value, 23);
}
