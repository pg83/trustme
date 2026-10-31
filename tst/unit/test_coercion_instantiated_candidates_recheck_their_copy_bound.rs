trait Inspect { fn byte_size(&self) -> usize; }
impl<T: Copy> Inspect for &T { fn byte_size(&self) -> usize { std::mem::size_of_val(*self) } }
fn total<V: Inspect>(values: &[V]) -> usize { values.iter().map(|v| v.byte_size()).sum() }
fn main() {
    let first = 17u16;
    let mut second = 19u16;
    assert_eq!(total(&[&first, &mut second]), 4);
    second += 1;
    assert_eq!(second, 20);
}
