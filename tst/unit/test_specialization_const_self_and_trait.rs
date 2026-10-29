#![feature(specialization)]
struct Choice<const N: usize>;
trait Kind<const M: usize> { fn kind() -> u8; }
impl<const N: usize, const M: usize> Kind<M> for Choice<N> {
    default fn kind() -> u8 { 1 }
}
impl<const N: usize> Kind<N> for Choice<N> { fn kind() -> u8 { 2 } }
fn main() {
    assert_eq!(<Choice<3> as Kind<3>>::kind(), 2);
    assert_eq!(<Choice<3> as Kind<4>>::kind(), 1);
    assert_eq!(<Choice<4> as Kind<3>>::kind(), 1);
}
