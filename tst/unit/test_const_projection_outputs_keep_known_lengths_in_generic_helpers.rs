//@ compile-variant: -O
trait Project { type Output; }
struct Identity<const N: usize>;
impl<const N: usize> Project for Identity<N> { type Output = [u16; N]; }
fn build<const N: usize>(value: u16) -> <Identity<N> as Project>::Output { [value; N] }
fn through<T: Project>(value: T::Output) -> T::Output { value }
fn main() {
    let first: [u16; 3] = through::<Identity<3>>(build::<3>(853));
    let second: [u16; 2] = build::<2>(857);
    assert_eq!(first, [853, 853, 853]);
    assert_eq!(second, [857, 857]);
}
