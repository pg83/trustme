struct Packet(u8, u16);
enum Code { Pair(u16, u8), Empty }
const PACKET: Packet = { let build = Packet; build(3, 257) };
const CODE: Code = { let build = Code::Pair; build(513, 7) };
fn main() {
    assert_eq!((PACKET.0, PACKET.1), (3, 257));
    match CODE { Code::Pair(a, b) => assert_eq!((a, b), (513, 7)), _ => panic!() }
}
