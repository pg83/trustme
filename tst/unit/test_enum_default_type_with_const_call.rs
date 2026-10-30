const fn width() -> usize { 3 }
enum Packet<T = [u8; width()]> {
    Data { bytes: T },
    Tuple(T),
}
impl Packet {
    fn make() -> Self { Self::Data { bytes: [17, 19, 23] } }
    fn tuple() -> Self { Self::Tuple([29, 31, 37]) }
}
fn main() {
    match Packet::make() {
        Packet::Data { bytes } => assert_eq!(bytes, [17, 19, 23]),
        Packet::Tuple(_) => panic!(),
    }
    match Packet::tuple() {
        Packet::Tuple(bytes) => assert_eq!(bytes, [29, 31, 37]),
        Packet::Data { .. } => panic!(),
    }
}
