#![feature(type_alias_impl_trait)]

type Reverse<const N: usize> = impl Fn([u16; N]) -> [u16; N];

struct Factory;

impl Factory {
    fn keep<const N: usize>(&self, callback: Reverse<N>) -> Reverse<N> {
        callback
    }

    #[define_opaque(Reverse)]
    fn make<const N: usize>(&self) -> Reverse<N> {
        self.keep::<N>(|mut input| {
            input.reverse();
            input
        })
    }
}

fn main() {
    let reverse = Factory.make::<4>();
    assert_eq!(reverse([13, 17, 19, 23]), [23, 19, 17, 13]);
    assert_eq!(Factory.make::<1>()([257]), [257]);
}
