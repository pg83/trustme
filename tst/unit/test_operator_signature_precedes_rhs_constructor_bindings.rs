use std::ops::Add;
struct Count(u8);
impl<'a> PartialEq<Option<&'a [u8]>> for Count {
    fn eq(&self, other: &Option<&'a [u8]>) -> bool { other.map(|v| v.len()) == Some(self.0 as usize) }
}
impl<'a> Add<Option<&'a [u8]>> for Count {
    type Output = usize;
    fn add(self, other: Option<&'a [u8]>) -> usize { self.0 as usize + other.map(|v| v.len()).unwrap_or(0) }
}
#[derive(PartialEq)]
struct Packet<T> { bytes: T }
fn main() {
    let present: Option<&[u8]> = Some(&[3, 5]);
    let absent: Option<&[u16]> = None;
    assert!(present != Some(b""));
    assert!(present == Some(&[3, 5]));
    assert!(absent == None);
    assert!(Count(2) == Some(b"xy"));
    assert_eq!(Count(5) + Some(b"xy"), 7);
    let packet: Packet<&[u8]> = Packet { bytes: &[3, 5] };
    assert!(packet == Packet { bytes: &[3, 5] });
    let tuple: (&[u8],) = (&[3, 5],);
    assert!(tuple == (&[3, 5],));
}
