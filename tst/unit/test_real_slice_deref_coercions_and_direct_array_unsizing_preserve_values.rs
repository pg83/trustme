use std::ops::{Deref, DerefMut};
struct Layer<T>(T);
impl<T> Deref for Layer<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Layer<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
fn sum(values: &[u32]) -> u32 { values.iter().sum() }
fn change(values: &mut [u32]) { values[1] += 1; }
trait Base { fn value(&self) -> u32; }
trait Sub: Base {}
impl Base for u32 { fn value(&self) -> u32 { *self } }
impl Sub for u32 {}
struct Tail<T: ?Sized> { head: u8, tail: T }

fn main() {
    let slice: Box<[u32]> = Box::new([11u32, 17, 23]);
    let mut layer = Layer(slice);
    assert_eq!(sum(&layer), 51);
    change(&mut layer);
    assert_eq!(sum(&layer), 52);
    let mut array = [29u32, 31];
    assert_eq!(sum(&array), 60);
    let raw: *mut [u32; 2] = &mut array;
    let weakened: *const [u32] = raw;
    assert_eq!(unsafe { &*weakened }, &[29, 31]);
    let tail = Tail { head: 7, tail: [37u32, 41] };
    let view: &Tail<[u32]> = &tail;
    assert_eq!((view.head, view.tail.len()), (7, 2));
    let object: Box<dyn Sub> = Box::new(43u32);
    let base: Box<dyn Base> = object;
    assert_eq!(base.value(), 43);
}
