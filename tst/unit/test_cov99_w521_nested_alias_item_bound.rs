#![feature(trait_alias)]

trait Source {
    type Item;
    fn read(&self) -> Self::Item;
}

trait SourceAlias = Source;
trait CopySource = SourceAlias<Item: Copy>;

struct Byte(u8);

impl Source for Byte {
    type Item = u8;

    fn read(&self) -> u8 {
        self.0
    }
}

fn read(source: &dyn CopySource<Item = u8>) -> u8 {
    source.read()
}

fn main() {
    assert_eq!(read(&Byte(37)), 37);
}
