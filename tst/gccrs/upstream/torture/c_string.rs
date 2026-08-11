// { dg-output "gccrs" }
// { dg-additional-options "-frust-edition=2021" }
use std::ffi::CStr;
fn gccrs_main() -> i32 {
    let value: &CStr = c"gccrs";
    print!("{}", value.to_str().unwrap());
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
