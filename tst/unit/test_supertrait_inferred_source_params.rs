trait Base<T, const N: usize> { type Output; fn value(&self, input: T) -> Self::Output; }
trait Derived<T, const N: usize>: Base<T, N> {}
impl Base<u8, 3> for () { type Output = [u8; 3]; fn value(&self, input: u8) -> [u8; 3] { [input; 3] } }
impl Derived<u8, 3> for () {}
fn object() -> &'static dyn Derived<u8, 3, Output=[u8; 3]> { &() }
fn opaque() -> impl Derived<u8, 3, Output=[u8; 3]> { () }
fn call<T, const N: usize, S: Base<T, N> + ?Sized>(value: &S, input: T) -> S::Output { value.value(input) }
fn main() {
    let object: &dyn Derived<_, 3, Output=_> = object();
    assert_eq!(call::<_, 3, _>(object, 7), [7; 3]);
    let opaque = opaque();
    assert_eq!(call::<_, 3, _>(&opaque, 11), [11; 3]);
}
