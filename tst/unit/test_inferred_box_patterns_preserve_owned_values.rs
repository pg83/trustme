#![feature(box_patterns)]
fn main() {
    let unwrap = |box value| value;
    assert_eq!(unwrap(Box::new(String::from("owned"))), "owned");
    let nested = |box (first, last)| first + last;
    assert_eq!(nested(Box::new((23u32, 29u32))), 52);
}
