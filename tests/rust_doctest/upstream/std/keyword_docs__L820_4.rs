// Extracted from library/std/src/keyword_docs.rs:820
#![allow(unused)]
#![allow(unused_assignments)]
fn main() {
    let thing1: i32 = 100;
    let thing2 = 200 + thing1;

    let mut changing_thing = true;
    changing_thing = false;

    let (part1, part2) = ("first", "second");

    struct Example {
        a: bool,
        b: u64,
    }

    let Example { a, b: _ } = Example {
        a: true,
        b: 10004,
    };
    assert!(a);
}
