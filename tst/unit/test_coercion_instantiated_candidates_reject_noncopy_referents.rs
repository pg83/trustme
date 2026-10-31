//@ compile-fail: Failed to find an impl of
trait Inspect { fn byte_size(&self) -> usize; }
impl<T: Copy> Inspect for &T { fn byte_size(&self) -> usize { std::mem::size_of_val(*self) } }
fn total<V: Inspect>(values: &[V]) -> usize { values.iter().map(|v| v.byte_size()).sum() }
fn main() {
    let first = String::from("first");
    let mut second = String::from("second");
    assert_eq!(total(&[&first, &mut second]), 4);
}
