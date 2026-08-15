trait HasAssoc {
    type Assoc;
}

impl HasAssoc for () {
    type Assoc = ();
}

trait Callable<I, O>: Fn(I) -> Option<O> {}

impl<I, O, F: Fn(I) -> Option<O>> Callable<I, O> for F {}

fn make<T: HasAssoc>() -> impl Callable<T, T::Assoc> {
    |_| None
}

fn main() {
    make()(());
}
