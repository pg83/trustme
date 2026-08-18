// A tuple struct written as a braced literal names its fields by index, and can
// take a base to fill in the rest.
#[derive(Debug, PartialEq)]
struct Color(u8, u8, u8);

#[derive(Debug, PartialEq)]
struct Pair(String, u32);

fn main() {
    let c2 = Color { 0: 255, 1: 127, 2: 0 };
    assert_eq!(c2, Color(255, 127, 0));

    let c3 = Color { 1: 9, ..c2 };
    assert_eq!(c3, Color(255, 9, 0));

    let c4 = Color { ..c3 };
    assert_eq!(c4, Color(255, 9, 0));

    // A field that is not `Copy` comes from the base by move.
    let p1 = Pair(String::from("left"), 1);
    let p2 = Pair { 1: 2, ..p1 };
    assert_eq!(p2, Pair(String::from("left"), 2));
}
