// Matching `Storage<T, N>` variants died with a value-count mismatch because
// pattern paths didn't size the const-generic value list.
enum Storage<T, const N: usize> { Inline([T; N], usize), Heap(Vec<T>) }
struct SmallVec<T, const N: usize>(Storage<T, N>);
impl<T: Copy + Default, const N: usize> SmallVec<T, N> {
    fn new() -> Self { Self(Storage::Inline([T::default(); N], 0)) }
    fn len(&self) -> usize {
        match &self.0 { Storage::Inline(_, n) => *n, Storage::Heap(v) => v.len() }
    }
}
fn main() {
    let s: SmallVec<u8, 4> = SmallVec::new();
    assert_eq!(s.len(), 0);
}
