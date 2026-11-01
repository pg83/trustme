const fn width(value: usize) -> usize { value + 1 }
struct Packet<const N: usize>([u16; N]);
fn make() -> Packet<{ width(3) }> { Packet([7; width(3)]) }

fn main() {
    let value: Packet<{ width(3) }> = make();
    assert_eq!(value.0.len(), 4);
    assert_eq!(value.0[3], 7);
    let smaller: Packet<{ width(0) }> = Packet([11]);
    assert_eq!(smaller.0[0], 11);
}
