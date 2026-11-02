//@ compile-fail: mix of deref patterns and normal constructors
#![feature(box_patterns)]
fn pick(value: Box<(u16, u16)>) -> u16 {
    match value {
        box (3, second) => second,
        Box { .. } => 7,
    }
}
fn main() {
    assert_eq!(pick(Box::new((3, 5))), 5);
    assert_eq!(pick(Box::new((11, 13))), 7);
}
