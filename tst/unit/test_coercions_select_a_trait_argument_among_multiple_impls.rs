trait Count<T> { fn count(values: &[T]) -> usize; }
struct Measure;
impl Count<&[u16]> for Measure { fn count(values: &[&[u16]]) -> usize { values.iter().map(|value| value.len()).sum() } }
impl Count<&str> for Measure { fn count(values: &[&str]) -> usize { values.iter().map(|value| value.len()).sum() } }
fn count<T>(values: &[T]) -> usize where Measure: Count<T> { Measure::count(values) }
fn main() {
    let left = [2u16, 3];
    let right = [5u16, 7, 11];
    assert_eq!(count(&[&left[..], &right]), 5);
    assert_eq!(count(&["ab", "cde"]), 5);
}
