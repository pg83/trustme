/* { dg-output "a == b\r*\na != c\r*\n" } */
struct Foo<T> { value: T }
impl PartialEq<Foo<u32>> for Foo<i32> { fn eq(&self, other: &Foo<u32>) -> bool { self.value >= 0 && self.value as u32 == other.value } }
fn gccrs_main() -> i32 {
    let a = Foo { value: 42i32 }; let b = Foo { value: 42u32 }; let c = Foo { value: 7u32 };
    println!("{}", if a == b { "a == b" } else { "a != b" });
    println!("{}", if a == c { "a == c" } else { "a != c" });
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
