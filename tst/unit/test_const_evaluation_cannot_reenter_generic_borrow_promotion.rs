//@ compile-fail: error:0:

struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    const VALUE: usize = {
        let borrowed = &Holder::<3>::VALUE;
        *borrowed
    };
}

const ROOT: usize = Holder::<3>::VALUE;

fn main() {
    let _ = ROOT;
}
