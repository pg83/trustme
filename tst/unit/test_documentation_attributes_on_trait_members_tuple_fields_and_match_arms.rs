struct Packet(
    #[doc = "Stored payload."]
    u16,
);

trait Read {
    #[doc = "Read the stored payload."]
    fn read(&self) -> u16;
}

impl Read for Packet {
    fn read(&self) -> u16 {
        self.0
    }
}

fn main() {
    let value = Packet(23).read();
    let result = match value {
        #[doc = "The selected payload."]
        23 => 7,
        _ => 0,
    };
    assert_eq!(result, 7);
}
