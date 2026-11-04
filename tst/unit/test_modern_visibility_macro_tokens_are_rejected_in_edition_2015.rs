//@ edition: 2015
//@ aux-build: modern_visibility_tokens.rs
//@ compile-fail: visibilit

#[macro_use]
extern crate modern_visibility_tokens;
mod outer {
    mod inner { relative!(value); }
}

fn main() {}
