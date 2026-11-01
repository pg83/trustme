struct Packet<T: ?Sized> { tag: u8, payload: T }

fn read(packet: &Packet<[u16]>) -> (u16, usize) {
    match packet {
        Packet { tag: 7, payload: [first, middle @ .., last] } => (*first + *last, middle.len()),
        _ => (0, 0),
    }
}

fn update(packet: &mut Packet<[u16]>) {
    if let Packet { payload: [_, middle @ .., last], .. } = packet {
        *last += middle.iter().copied().sum::<u16>();
    }
}

fn main() {
    let mut packet = Packet { tag: 7, payload: [3, 5, 11, 17] };
    assert_eq!(read(std::hint::black_box(&packet)), (20, 2));
    update(std::hint::black_box(&mut packet));
    assert_eq!(packet.payload, [3, 5, 11, 33]);
    assert_eq!(read(&Packet { tag: 7, payload: [13] }), (0, 0));
}
