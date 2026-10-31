struct Unit;
struct Packet(u8, u8, u8, u8);
struct Named { first: u8, middle: u8, last: u8 }
#[inline(never)]
fn tuple(value: (u8, u8, u8, u8)) -> u8 {
    match value { (3, .., last) => last, (_, first, second, ..) => first + second }
}
fn main() {
    assert_eq!(tuple((3, 5, 7, 11)), 11);
    assert_eq!(tuple((13, 5, 7, 11)), 12);
    let Packet(first, .., last) = Packet(3, 5, 7, 11);
    assert_eq!(first + last, 14);
    let Packet { .. } = Packet(13, 17, 19, 23);
    let Unit {} = Unit;
    let Named { first, .. } = Named { first: 29, middle: 31, last: 37 };
    assert_eq!(first, 29);
}
