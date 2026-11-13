trait Choose {
    type Left;
    type Right;
}

struct Key;
impl Choose for Key {
    type Left = u8;
    type Right = u8;
}

trait Read<T> {
    fn read(&self) -> u32;
}

trait Both<C: Choose>: Read<C::Left> + Read<C::Right> {}

struct Source;
impl Read<u8> for Source {
    fn read(&self) -> u32 { 41 }
}
impl Both<Key> for Source {}

fn main() {
    let value: &dyn Both<Key> = &Source;
    assert_eq!(<dyn Both<Key> as Read<u8>>::read(value), 41);
}
