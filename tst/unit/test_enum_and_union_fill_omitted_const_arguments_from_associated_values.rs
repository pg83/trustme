struct Width;

impl Width {
    const BYTES: usize = 4;
}

enum Packet<const N: usize = { Width::BYTES }> {
    Bytes([u8; N]),
}

union Storage<const N: usize = { Width::BYTES }> {
    bytes: [u8; N],
    word: u32,
}

fn packet() -> Packet {
    Packet::Bytes([157, 163, 167, 173])
}

fn storage() -> Storage {
    Storage { bytes: [179, 181, 191, 193] }
}

fn main() {
    let Packet::Bytes(bytes) = packet();
    assert_eq!(bytes, [157, 163, 167, 173]);
    let value = storage();
    assert_eq!(unsafe { value.bytes }, [179, 181, 191, 193]);
    assert_eq!(core::mem::size_of_val(&value), 4);
    let explicit: Storage<8> = Storage { bytes: [197; 8] };
    assert_eq!(unsafe { explicit.bytes }, [197; 8]);
}
