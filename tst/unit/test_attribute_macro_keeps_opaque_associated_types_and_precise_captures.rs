//@ proc-macro-aux-build: preserve_bound_types.rs
use preserve_bound_types::preserve;

trait Read { type Item; fn read(&self) -> Self::Item; }
impl<T: Copy> Read for &T { type Item = T; fn read(&self) -> T { **self } }

#[preserve]
fn view<'a, T: Copy>(value: &'a T) -> impl Read<Item = T> + Copy + use<'a, T> { value }

fn copied<T: Copy>(value: T) -> T { value }

fn main() {
    let value = 43u16;
    let first = view(&value);
    let second = copied(first);
    assert_eq!((first.read(), second.read()), (43, 43));
}
