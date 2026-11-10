#![feature(type_alias_impl_trait, generic_const_exprs, non_lifetime_binders)]
#![allow(incomplete_features)]

type Stream<T, const N: usize> = impl Iterator<Item = (T, [u8; N])>;

#[define_opaque(Stream)]
fn generate<T, const N: usize>(value: T) -> Stream<T, N> {
    core::iter::once((value, [17; N]))
}

trait Courier<X, const N: usize> {
    fn send(value: [u8; N]) -> [u8; N];
}

struct Mail;

impl<X, const N: usize> Courier<X, N> for Mail {
    fn send(value: [u8; N]) -> [u8; N] {
        value
    }
}

fn relay<F, const N: usize>(value: [u8; N]) -> [u8; N + 1]
where
    F: for<X> Courier<(X, *const X), N>,
    [(); N + 1]:,
{
    let input = <F as Courier<(u32, *const u32), N>>::send(value);
    let mut output = [31; N + 1];
    output[..N].copy_from_slice(&input);
    output
}

fn main() {
    let mut values = generate::<u16, 3>(23);
    assert_eq!(values.next(), Some((23, [17; 3])));
    assert_eq!(values.next(), None);
    assert_eq!(relay::<Mail, 4>([7, 11, 13, 17]), [7, 11, 13, 17, 31]);
}
