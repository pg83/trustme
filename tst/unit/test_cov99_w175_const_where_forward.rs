//@ crate-type: lib
#![feature(const_trait_impl)]

pub const trait ByteSource {
    fn byte(&self) -> u8;
}

pub trait ReadByte {
    fn read(&self) -> u8;
}

pub struct Reader<T>(pub T);

impl<T> ReadByte for Reader<T>
where
    T: const ByteSource,
{
    fn read(&self) -> u8 {
        self.0.byte()
    }
}
