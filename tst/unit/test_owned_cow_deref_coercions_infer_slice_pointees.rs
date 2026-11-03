use std::borrow::Cow;
fn count(values: &[u16]) -> usize { values.len() }
fn total(values: &[u16]) -> u16 { values.iter().copied().sum() }
fn main() {
    assert_eq!(count(&Cow::Owned(vec![11u16, 17, 23])), 3);
    assert_eq!(total(&Cow::Owned(vec![29u16, 31])), 60);
    assert_eq!(total(&Cow::Borrowed(&[37u16, 41][..])), 78);
}
