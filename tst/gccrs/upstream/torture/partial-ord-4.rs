/* { dg-output "a == b\r*\na != c\r*\n" } */
#[derive(PartialEq, PartialOrd)] struct Foo { a: i32 }
fn gccrs_main() -> i32 {
    let a=Foo{a:42}; let b=Foo{a:42}; let c=Foo{a:7};
    println!("{}", if a==b {"a == b"} else {"a != b"});
    println!("{}", if a==c {"a == c"} else {"a != c"});
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
