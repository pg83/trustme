#![feature(box_patterns)]

fn payload(value: Box<(u16, u16)>) -> u16 {
    match value {
        box (3, second) => second,
        _ => 7,
    }
}

fn wildcard(value: Box<(u16, u16)>) -> u16 {
    match value {
        Box { .. } => 11,
    }
}

fn main() {
    assert_eq!(payload(Box::new((3, 5))), 5);
    assert_eq!(payload(Box::new((13, 17))), 7);
    assert_eq!(wildcard(Box::new((19, 23))), 11);
}
