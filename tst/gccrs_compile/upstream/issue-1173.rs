pub trait Hasher {
    fn write(&mut self, bytes: &[u8]);

    fn write_u16(&mut self, value: u16) {
        self.write(&value.to_ne_bytes())
    }
}

pub struct SipHasher;

impl Hasher for SipHasher {
    fn write(&mut self, _message: &[u8]) {}
}
