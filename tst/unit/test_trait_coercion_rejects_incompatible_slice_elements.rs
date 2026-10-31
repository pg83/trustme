//@ compile-fail: Type mismatch between
trait Values { fn values(&self) -> &[u16]; }
impl Values for &[u16] { fn values(&self) -> &[u16] { self } }
fn total<V: Values>(groups: &[V]) -> u16 {
    groups.iter().flat_map(|group| group.values().iter()).copied().sum()
}
fn main() {
    let first = [2u16, 3];
    let second = [5, 7, 11];
    let third = [13u32];
    assert_eq!(total(&[&first[..], &second, &third]), 41);
}
