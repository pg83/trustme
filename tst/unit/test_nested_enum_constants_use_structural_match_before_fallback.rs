#[derive(PartialEq, Eq)]
enum Shape { Pair(u8, u8), Named { value: u8 }, Empty }
const PAIR: Shape = Shape::Pair(17, 23);
const NAMED: Shape = Shape::Named { value: 37 };
const EMPTY: Shape = Shape::Empty;
fn select(value: Shape) -> u8 {
    match value { PAIR => 1, NAMED => 2, EMPTY => 3, _ => 4 }
}
#[derive(PartialEq, Eq)]
struct Unit;
const UNIT: Unit = Unit;
fn main() {
    assert_eq!(select(Shape::Pair(17, 23)), 1);
    assert_eq!(select(Shape::Pair(17, 24)), 4);
    assert_eq!(select(Shape::Named { value: 37 }), 2);
    assert_eq!(select(Shape::Named { value: 38 }), 4);
    assert_eq!(select(Shape::Empty), 3);
    assert!(matches!(Unit, UNIT));
}
