#![feature(never_type)]
fn nested(value: &&!) -> u32 { match value { &&_ => 17 } }
fn tuple(value: (bool, &!)) -> u32 {
    match value { (true | false, &_) => 23 }
}
fn main() {
    let first: fn(&&!) -> u32 = nested;
    let second: fn((bool, &!)) -> u32 = tuple;
    assert_eq!(std::mem::size_of_val(&first), std::mem::size_of_val(&second));
}
