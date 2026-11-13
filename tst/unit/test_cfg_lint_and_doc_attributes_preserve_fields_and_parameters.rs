struct Packet {
    #[cfg(all())]
    #[allow(dead_code)]
    #[doc = "Stored value"]
    value: u16,
}

fn read(#[cfg(all())] #[allow(unused_variables)] value: u16) -> u16 {
    value + 11
}

fn main() {
    let packet = Packet { value: 661 };
    assert_eq!(read(packet.value), 672);
}
