//@ compile-fail: error:0: cycle detected when evaluating static

struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    const VALUE: usize = {
        let borrowed = &Holder::<3>::VALUE;
        *borrowed
    };
}

fn main() {
    let _ = Holder::<3>::VALUE;
}
