//@ compile-flags: -Z dump-mir

#[derive(Copy, Clone)]
struct Marker;

struct Packet {
    marker: Marker,
    value: u32,
}

#[inline(never)]
fn read_value(packet: &Packet) -> u32 {
    packet.value
}

#[inline(never)]
fn read_marker(packet: &Packet) -> Marker {
    packet.marker
}

fn main() {
    let packet = Packet { marker: Marker, value: 41 };
    let _marker = read_marker(&packet);
    assert_eq!(read_value(&packet) + 1, 42);
}
