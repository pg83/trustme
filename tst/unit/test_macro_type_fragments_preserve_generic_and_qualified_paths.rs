struct Packet<T>(T);
trait Family { type Item; }
impl Family for Packet<u16> { type Item = u16; }
macro_rules! typed {
    ($ty:ty, $value:expr) => {{ let value: $ty = $value; value }};
}
macro_rules! word { () => { u32 }; }
mod nested {
    pub fn check() {
        let packet = typed!(super::Packet<u8>, super::Packet(7));
        assert_eq!(packet.0, 7);
        assert_eq!(typed!(word!(), 11), 11);
        assert_eq!(typed!(word![], 13), 13);
        assert_eq!(typed!(word!{}, 17), 17);
    }
}
fn main() {
    assert_eq!(typed!(<Packet<u16> as Family>::Item, 19), 19);
    let packet = typed!(crate::Packet<::std::option::Option<u8>>, Packet(Some(23)));
    assert_eq!(packet.0, Some(23));
    let packet = typed!(self::Packet<Packet<u8>>, Packet(Packet(29)));
    assert_eq!(packet.0.0, 29);
    nested::check();
}
