#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait ArrayMaker<const N: usize> {
    fn make(&self) -> [u8; N + 1];
}

impl<const N: usize> ArrayMaker<N> for () {
    fn make(&self) -> [u8; N + 1] {
        [9_u8; N + 1]
    }
}

fn make_dyn<const N: usize>(maker: &dyn ArrayMaker<N>) -> [u8; N + 1]
where
    [u8; N + 1]: Sized,
{
    let values = maker.make();
    assert_eq!(values, [9_u8; N + 1]);
    values
}

fn main() {
    let values = make_dyn::<3>(&());
    assert!(values[0] == 9);
    assert!(values[3] == 9);
}
