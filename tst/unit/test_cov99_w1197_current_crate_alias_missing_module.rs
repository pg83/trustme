//@ edition: 2021
//@ compile-fail: Unknown macro

extern crate self as this_crate;

this_crate::missing::make!();

fn main() {}
