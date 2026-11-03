#![feature(type_alias_impl_trait)]
type Transform<T> = impl Fn(T) -> T;
#[define_opaque(Transform)]
fn create<T>() -> Transform<T> { |value| value }
fn main() {
    let number = create();
    let array = create();
    assert_eq!(number(239u32), 239);
    assert_eq!(array([3u16, 5, 7]), [3, 5, 7]);
}
