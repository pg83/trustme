use std::cmp::Ordering;

#[derive(PartialEq, PartialOrd)]
enum Foo { A, B(i32, i32, i32), C { inner: i32, outer: i32 } }

#[derive(Ord, PartialOrd, PartialEq, Eq)]
struct Bar { a: i32 }

#[derive(Ord, PartialOrd, PartialEq, Eq)]
struct BarFull { a: i32, b: i32, c: i32, d: i32 }

fn main() {
    let a = Foo::A; let b = Foo::B(15, 14, 13);
    let _ordering: Option<Ordering> = a.partial_cmp(&b);
}
