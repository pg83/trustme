#[derive(PartialEq, PartialOrd)]
struct Foo { a: i32 }

pub fn compare() -> bool {
    Foo { a: 1 } < Foo { a: 2 }
}
