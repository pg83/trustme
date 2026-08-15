#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]

fn value<const TEXT: &'static str>() -> &'static str {
    TEXT
}

fn main() {
    assert_eq!(value::<"hello">(), "hello");
}
