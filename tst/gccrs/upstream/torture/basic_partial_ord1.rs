/* { dg-output "less\r*" } */
use std::cmp::Ordering;
#[derive(PartialEq, Eq, PartialOrd, Ord)] struct Bar { a: i32, b: i32 }
fn gccrs_main() -> i32 {
    let x = Bar { a: 1, b: 2 }; let y = Bar { a: 1, b: 3 };
    println!("{}", match x.partial_cmp(&y) { Some(Ordering::Less) => "less", Some(Ordering::Greater) => "greater", Some(Ordering::Equal) => "equal", None => "none" });
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
