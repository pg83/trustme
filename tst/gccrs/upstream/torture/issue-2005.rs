/* { dg-output "WORKS\r?\n" } */
#[derive(Ord, PartialOrd, PartialEq, Eq)] enum Foo { A, B(i32) }
fn gccrs_main() -> i32 { if Foo::A != Foo::B(15) { println!("WORKS"); } 0 }
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
