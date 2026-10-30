//@ compile-fail: type annotations needed
trait Read<T> { fn read(&self) -> T; }
impl Read<u16> for u8 { fn read(&self) -> u16 { *self as u16 } }
impl Read<u32> for u8 { fn read(&self) -> u32 { *self as u32 } }
fn inspect<T: Read<U> + ?Sized, U>(_: &T) {}
fn main() {
    let object: &(dyn Read<_> + Send + Sync) = &7u8;
    let alias = object;
    inspect(alias);
}
