//@ edition: 2015
//@ aux-build: modern_visibility_tokens.rs
//@ compile-variant: -O

#[macro_use]
extern crate modern_visibility_tokens;
mod outer {
    mod inner { forward!(pub(in outer) fn value); }
    pub fn value() -> u16 { inner::value() + 2 }
}

fn main() { assert_eq!(outer::value(), 139); }
