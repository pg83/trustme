#![feature(box_patterns)]
#[inline(never)]
fn inspect(value: Box<Option<(u8, u8)>>) -> u8 {
    match value { box Some((3, last)) => last, box Some((first, _)) => first, box None => 0 }
}
fn main() {
    assert_eq!(inspect(Box::new(Some((3, 37)))), 37);
    assert_eq!(inspect(Box::new(Some((41, 43)))), 41);
    assert_eq!(inspect(Box::new(None)), 0);
}
