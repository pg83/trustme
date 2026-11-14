//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Wrap<const N: usize>;

trait Rel<T> {
    fn make() -> T;
}

impl<const N: usize> Rel<(Wrap<N>, Wrap<3>)> for Wrap<N> {
    fn make() -> (Wrap<N>, Wrap<3>) {
        (Wrap, Wrap)
    }
}

fn choose<const N: usize, T>(_: Wrap<N>) -> T
where
    Wrap<N>: Rel<(T, Wrap<N>)>,
{
    <Wrap<N> as Rel<(T, Wrap<N>)>>::make().0
}

pub fn exercise() {
    let _inferred = choose(Wrap);
}
