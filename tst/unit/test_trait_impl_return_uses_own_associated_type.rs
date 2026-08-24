use std::fmt::Debug;

#[derive(Clone, Copy)]
struct Format;

#[derive(Debug)]
struct Error;

type Result<T> = std::result::Result<T, Error>;

trait ReaderOffset: Copy {}

impl ReaderOffset for usize {}

trait Reader: Debug + Clone {
    type Endian;
    type Offset: ReaderOffset;

    fn read_offset(&mut self, format: Format) -> Result<Self::Offset>;
}

trait Relocate<T: ReaderOffset = usize> {
    fn relocate_offset(&self, value: T) -> Result<T>;
}

#[derive(Debug, Clone)]
struct RelocateReader<R: Reader<Offset = usize>, T: Relocate<R::Offset>> {
    reader: R,
    relocate: T,
}

impl<R, T> Reader for RelocateReader<R, T>
where
    R: Reader<Offset = usize>,
    T: Relocate<R::Offset> + Debug + Clone,
{
    type Endian = R::Endian;
    type Offset = R::Offset;

    fn read_offset(&mut self, format: Format) -> Result<R::Offset> {
        let value = self.reader.read_offset(format)?;
        self.relocate.relocate_offset(value)
    }
}

fn main() {}
