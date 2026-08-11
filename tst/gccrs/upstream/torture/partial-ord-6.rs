/* { dg-output "Foo A < B\r?\nFoo B < C\r?\nFoo C == C\r?\nBar x < y\r?\nBarFull s1 < s2\r?\n" } */
use std::cmp::Ordering;
#[derive(PartialEq, Eq, PartialOrd, Ord)] enum Foo { A, B(i32,i32,i32), C { inner:i32, outer:i32 } }
#[derive(PartialEq, Eq, PartialOrd, Ord)] struct Bar { a:i32 }
#[derive(PartialEq, Eq, PartialOrd, Ord)] struct BarFull { a:i32,b:i32,c:i32,d:i32 }
fn gccrs_main() -> i32 {
    let a=Foo::A; let b=Foo::B(15,14,13); let c=Foo::C{inner:10,outer:20};
    if a<b {println!("Foo A < B");} if b<c {println!("Foo B < C");} if c==c {println!("Foo C == C");}
    let x=Bar{a:10}; let y=Bar{a:20}; if x<y {println!("Bar x < y");}
    let s1=BarFull{a:1,b:2,c:3,d:4}; let s2=BarFull{a:1,b:2,c:3,d:5};
    if s1.cmp(&s2)==Ordering::Less {println!("BarFull s1 < s2");}
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
