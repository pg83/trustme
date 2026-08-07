// Extracted from library/core/src/fmt/mod.rs:1279
#![allow(unused)]
fn main() {
    let x = &42;

    let address = format!("{x:p}"); // this produces something like '0x7f06092ac6d0'
}
