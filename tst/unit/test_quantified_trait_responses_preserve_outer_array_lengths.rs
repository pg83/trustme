#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Courier<T> {
    type Output;
    fn pass(value: T) -> Self::Output;
}

struct Bridge;

impl<T> Courier<T> for Bridge {
    type Output = T;

    fn pass(value: T) -> T {
        value
    }
}

fn through<M, const N: usize>(value: [u16; N]) -> [u16; N]
where
    M: for<T> Courier<(T, [u16; N]), Output = (T, [u16; N])>,
{
    let (token, value) = <M as Courier<(u8, [u16; N])>>::pass((91, value));
    assert_eq!(token, 91);
    let (label, value) = <M as Courier<(&str, [u16; N])>>::pass(("parcel", value));
    assert_eq!(label, "parcel");
    value
}

fn main() {
    assert_eq!(through::<Bridge, 3>([37, 41, 43]), [37, 41, 43]);
    assert_eq!(through::<Bridge, 0>([]), []);
}
