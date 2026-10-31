#![feature(deref_patterns)]
#[inline(never)]
fn text(value: String) -> u8 { match value { deref!("fir") => 7, _ => 11 } }
#[inline(never)]
fn boxed(value: Box<(u8, u8)>) -> u8 { match value { deref!((3, last)) => last, _ => 0 } }
fn main() {
    assert_eq!(text(String::from("fir")), 7);
    assert_eq!(text(String::from("elm")), 11);
    assert_eq!(boxed(Box::new((3, 37))), 37);
    assert_eq!(boxed(Box::new((5, 41))), 0);
}
