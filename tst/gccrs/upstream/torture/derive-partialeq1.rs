// { dg-output "true\r*\nfalse\r*\nfalse\r*\n" }
#[derive(PartialEq, Clone, Copy)] struct Foo;
#[derive(PartialEq)] struct Bar(Foo);
#[derive(PartialEq)] struct Baz { inner: Foo }
fn gccrs_main() -> i32 {
    let x = Foo;
    println!("{}", x == Foo);
    println!("{}", Bar(x) != Bar(Foo));
    println!("{}", Baz { inner: Foo } != Baz { inner: x });
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
