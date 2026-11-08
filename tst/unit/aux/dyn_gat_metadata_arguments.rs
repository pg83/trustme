pub trait Port {
    type Tag<T> where Self: Sized;
    fn read(&self) -> u32;
}

pub struct Beacon(pub u32);

impl Port for Beacon {
    type Tag<T> = u32;

    fn read(&self) -> u32 {
        self.0
    }
}

static BYTE: Beacon = Beacon(43);
static WORD: Beacon = Beacon(67);

pub fn byte_port() -> &'static dyn Port<Tag<u8> = u32> {
    &BYTE
}

pub fn word_port() -> &'static dyn Port<Tag<u16> = u32> {
    &WORD
}
