//@ aux-build: hygienic_callable_value.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    assert_eq!(hygienic_callable_value::invoke!(199u16), 201);
    assert_eq!(hygienic_callable_value::invoke!(211u16), 213);
}
