fn zero<T: Default>() -> T { T::default() }
fn main() {
    let (left, right) = zero();
    let left: u16 = left;
    let right: u64 = right;
    assert_eq!((left, right), (0, 0));
    let ((inner,), ()) = zero();
    let inner: u32 = inner;
    assert_eq!(inner, 0);
}
