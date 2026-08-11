/* { dg-output "x == y\r*\nx > z\r*\n" } */
use std::cmp::Ordering;
#[derive(PartialEq, PartialOrd)] struct Foo { a: i32 }
fn describe(left: &Foo, right: &Foo, names: (&str, &str)) {
    println!("{}", match left.partial_cmp(right) { Some(Ordering::Equal) => format!("{} == {}", names.0, names.1), Some(Ordering::Less) => format!("{} < {}", names.0, names.1), Some(Ordering::Greater) => format!("{} > {}", names.0, names.1), None => format!("{} ? {}", names.0, names.1) });
}
fn gccrs_main() -> i32 { let x=Foo{a:42}; let y=Foo{a:42}; let z=Foo{a:7}; describe(&x,&y,("x","y")); describe(&x,&z,("x","z")); 0 }
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
