/* { dg-output "a == b\r*\na != c\r*\na >= c\r*\na <= b\r*\na > c\r*\nc < b\r*\n" } */
#[derive(PartialEq, PartialOrd)] struct Foo { a: i32 }
fn gccrs_main() -> i32 {
    let a=Foo{a:42}; let b=Foo{a:42}; let c=Foo{a:7};
    if a==b { println!("a == b"); } if a!=c { println!("a != c"); }
    if a>=c { println!("a >= c"); } if a<=b { println!("a <= b"); }
    if a>c { println!("a > c"); } if c<b { println!("c < b"); }
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
