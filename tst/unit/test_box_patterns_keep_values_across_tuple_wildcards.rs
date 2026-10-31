#![feature(box_patterns)]
fn select(value: (Box<u8>, u8)) -> u8 {
    match value { (box (17 | 23), extra) => extra + 1, (_, extra) => extra + 2 }
}
fn main() {
    assert_eq!(select((Box::new(17), 3)), 4);
    assert_eq!(select((Box::new(23), 5)), 6);
    assert_eq!(select((Box::new(37), 7)), 9);
    let (box inner, _) = (Box::new(41), 0u8);
    assert_eq!(inner, 41);
}
