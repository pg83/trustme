struct Packet<T>(T);
const WORD: fn(u16) -> Packet<u16> = Packet;
const PAIR: fn((u8, u16)) -> Packet<(u8, u16)> = Packet;
static TABLE: [fn(u16) -> Packet<u16>; 2] = [Packet, WORD];
#[inline(never)]
fn call(make: fn(u16) -> Packet<u16>, value: u16) -> u16 { make(value).0 }

fn main() {
    assert_eq!(WORD(17).0, 17);
    assert_eq!(PAIR((3, 19)).0, (3, 19));
    assert_eq!(TABLE[0](23).0, 23);
    assert_eq!(TABLE[1](29).0, 29);
    assert_eq!(call(WORD, 31), 31);
    assert_eq!(call(Packet, 37), 37);
}
