//@ compile-variant: -O
trait Packet { const BYTES: [u8; 4]; }
struct Amber;
struct Indigo;
impl Packet for Amber { const BYTES: [u8; 4] = [11, 23, 37, 41]; }
impl Packet for Indigo { const BYTES: [u8; 4] = [43, 59, 61, 73]; }
#[inline(never)]
fn choose<A: Packet, B: Packet>(flag: bool) -> [u8; 4] {
    let bytes;
    if flag { bytes = A::BYTES; } else { bytes = B::BYTES; }
    bytes
}
fn main() {
    assert_eq!(choose::<Amber, Indigo>(std::hint::black_box(true)), [11, 23, 37, 41]);
    assert_eq!(choose::<Amber, Indigo>(std::hint::black_box(false)), [43, 59, 61, 73]);
    assert_eq!(choose::<Indigo, Amber>(std::hint::black_box(true)), [43, 59, 61, 73]);
}
