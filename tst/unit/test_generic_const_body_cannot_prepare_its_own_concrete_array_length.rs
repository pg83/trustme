//@ compile-fail: error:0:
#![feature(generic_const_exprs)]

struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    const VALUE: usize = {
        let _: [u8; Holder::<3>::VALUE] = [];
        N
    };
}

fn main() {
    let _ = Holder::<3>::VALUE;
}
