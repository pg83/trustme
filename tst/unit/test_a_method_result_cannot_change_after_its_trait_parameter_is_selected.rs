//@ compile-fail: No applicable methods for
trait Mirror<T> {
    type Item;
    fn read(&self) -> <T as Mirror<T>>::Item
    where
        T: Mirror<T>;
}

impl Mirror<u8> for u8 {
    type Item = u16;
    fn read(&self) -> u16 { 67 }
}

fn main() {
    let _: bool = 3u8.read();
}
