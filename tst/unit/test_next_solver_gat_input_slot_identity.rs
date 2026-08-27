//@ crate-type: lib

// A GAT argument is an input of NormalizesTo even when it does not occur in
// the trait/impl head.  The output must contain that same inference variable,
// so a later operation on the normalized type can infer the method argument.
trait Family {
    type Wrapped<T>;

    fn make<T>() -> Self::Wrapped<T>;
}

struct Wrapped<T>(T);

impl<T> Wrapped<T> {
    fn accept(self, _: T) {}
}

impl Family for () {
    type Wrapped<T> = Wrapped<T>;

    fn make<T>() -> Self::Wrapped<T> {
        loop {}
    }
}

pub fn infer_gat_argument_from_output() {
    <() as Family>::make().accept(1u8);
}

trait ConstFamily {
    type Wrapped<const N: usize>;

    fn make<const N: usize>() -> Self::Wrapped<N>;
}

struct ConstWrapped<const N: usize>;

impl<const N: usize> ConstWrapped<N> {
    fn accept(self, _: [u8; N]) {}
}

impl ConstFamily for () {
    type Wrapped<const N: usize> = ConstWrapped<N>;

    fn make<const N: usize>() -> Self::Wrapped<N> {
        ConstWrapped
    }
}

pub fn infer_const_gat_argument_from_output() {
    <() as ConstFamily>::make().accept([0; 3]);
}
