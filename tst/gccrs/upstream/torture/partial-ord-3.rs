/* { dg-output "x == y\r*\nx > z\r*\nx < z\r*\nx >= y\r*\nx <= y\r*\n" } */
use std::cmp::Ordering;
#[derive(PartialEq, Eq, PartialOrd, Ord)] struct Foo { a: i32 }
fn gccrs_main() -> i32 {
    let x=Foo{a:42}; let y=Foo{a:42}; let z=Foo{a:7};
    if x==y { println!("x == y"); }
    if x.partial_cmp(&z)==Some(Ordering::Greater) { println!("x > z"); }
    if z<x { println!("x < z"); } if x>=y { println!("x >= y"); } if x<=y { println!("x <= y"); }
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
