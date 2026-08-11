// { dg-output "true\r*\nfalse\r*\nfalse\r*\nfalse\r*\nfalse\r*\n" }
#[derive(PartialEq)] enum Foo { A { a: i32, b: i32 }, B(i32, i32), C }
fn gccrs_main() -> i32 {
    let x = Foo::A { a: 15, b: 14 };
    println!("{}", x == Foo::A { a: 15, b: 14 });
    println!("{}", x == Foo::A { a: 15, b: 19 });
    println!("{}", x == Foo::A { a: 19, b: 14 });
    println!("{}", x == Foo::B(15, 14));
    println!("{}", x == Foo::C);
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
