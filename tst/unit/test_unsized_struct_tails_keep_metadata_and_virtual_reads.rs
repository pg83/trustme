//@ compile-variant: -O

#[repr(C)]
struct Tagged<T: ?Sized> { tag: u16, tail: T }
trait Read { fn read(&self, index: usize) -> u16; }
impl Read for Tagged<[u16]> {
    fn read(&self, index: usize) -> u16 { self.tag + self.tail[index] }
}
#[inline(never)]
fn inspect(value: &Tagged<[u16]>) -> (usize, u16) {
    let object: &dyn Read = &value;
    (value.tail.len(), object.read(1))
}
impl Read for &Tagged<[u16]> {
    fn read(&self, index: usize) -> u16 { self.tag + self.tail[index] }
}
fn main() {
    let value = Tagged { tag: 11, tail: [3, 5, 7, 13] };
    let view: &Tagged<[u16]> = &value;
    assert_eq!(inspect(view), (4, 16));
    assert_eq!(view.read(3), 24);
}
