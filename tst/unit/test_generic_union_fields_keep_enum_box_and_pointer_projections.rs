//@ compile-variant: -O
struct Pair<T> { first: T, second: T }
union Choice<T: Copy> { element: T, padding: u64 }
enum Packet<T> { Full { slots: Pair<T> }, Empty }
#[inline(never)]
fn read(packet: &Packet<Choice<u16>>) -> u16 {
    match packet {
        Packet::Full { slots } => unsafe { slots.first.element + slots.second.element },
        Packet::Empty => 0,
    }
}
fn main() {
    let mut packet = Box::new(Packet::Full { slots: Pair {
        first: Choice { element: 563u16 }, second: Choice { element: 569u16 },
    } });
    assert_eq!(read(&packet), 1132);
    let raw: *mut Packet<Choice<u16>> = &mut *packet;
    unsafe {
        match &mut *raw {
            Packet::Full { slots } => slots.second.element = 571,
            Packet::Empty => panic!("missing packet"),
        }
    }
    assert_eq!(read(&packet), 1134);
    let empty: Packet<Choice<u16>> = Packet::Empty;
    assert_eq!(read(&empty), 0);
}
