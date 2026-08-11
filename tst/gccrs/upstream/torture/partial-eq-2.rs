/* { dg-output "a == b\r*\na != c\r*\n" } */
#[derive(PartialEq)] struct Foo<T> { value: T }
fn gccrs_main() -> i32 {
    let a = Foo { value: 42 }; let b = Foo { value: 42 }; let c = Foo { value: 99 };
    println!("{}", if a == b { "a == b" } else { "a != b" });
    println!("{}", if a == c { "a == c" } else { "a != c" });
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
