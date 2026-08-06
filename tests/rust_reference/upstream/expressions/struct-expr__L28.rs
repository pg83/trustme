// Extracted from src/expressions/struct-expr.md:28
#![allow(unused)]
fn main() {
    struct Point { x: f64, y: f64 }
    struct NothingInMe { }
    mod game { pub struct User<'a> { pub name: &'a str, pub age: u32, pub score: usize } }
    enum Enum { Variant {} }
    Point {x: 10.0, y: 20.0};
    NothingInMe {};
    let u = game::User {name: "Joe", age: 35, score: 100_000};
    Enum::Variant {};
}
