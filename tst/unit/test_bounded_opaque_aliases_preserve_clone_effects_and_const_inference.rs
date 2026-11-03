#![feature(type_alias_impl_trait)]
use std::cell::Cell;
use std::rc::Rc;
struct Count(Rc<Cell<u32>>);
impl Clone for Count {
    fn clone(&self) -> Self { self.0.set(self.0.get() + 1); Count(self.0.clone()) }
}
type Hidden<T: Clone> = impl Clone;
#[define_opaque(Hidden)]
fn hide<T: Clone>(value: T) -> Hidden<T> { value }
fn nested<T: Clone>(value: T) -> impl Clone { hide(value) }
fn values<const N: usize>() -> [u8; N] { [7; N] }
fn iter(values: &[u32]) -> impl Iterator<Item = &u32> { values.iter() }
fn closure() -> impl Fn(u32) -> u32 { |value| value + 5 }

fn main() {
    let clones = Rc::new(Cell::new(0));
    let first = hide(Count(clones.clone()));
    drop(first.clone());
    assert_eq!(clones.get(), 1);
    let second = nested(Count(clones.clone()));
    drop(second.clone());
    assert_eq!(clones.get(), 2);
    let array: [u8; 3] = values::<_>();
    assert_eq!(array, [7, 7, 7]);
    assert_eq!(iter(&[11, 13, 17]).copied().sum::<u32>(), 41);
    assert_eq!(closure()(19), 24);
}
