pub trait Hasher {
    fn finish(&self) -> u64;
    fn write(&mut self, bytes: &[u8]);

    fn write_u16(&mut self, value: u16) {
        self.write(&value.to_ne_bytes())
    }

    fn write_i16(&mut self, value: i16) {
        self.write_u16(value as u16)
    }
}

pub struct SipHasher;

impl Hasher for SipHasher {
    fn write(&mut self, _message: &[u8]) {}

    fn finish(&self) -> u64 {
        0
    }
}
