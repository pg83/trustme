//@ compile-variant: -O
fn same<T>(left: T, right: T) -> (T, T) { (left, right) }
fn integer(value: u16) -> u16 { value }
fn real(value: f32) -> f32 { value }
fn main() {
    let a = same(Default::default(), 181);
    let b = same(3.25, Default::default());
    assert_eq!(integer(a.0), 0);
    assert_eq!(integer(a.1), 181);
    assert_eq!(real(b.0), 3.25);
    assert_eq!(real(b.1), 0.0);
}
