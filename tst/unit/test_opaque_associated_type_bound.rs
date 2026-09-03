// `impl Tr1<As1: Clone>` puts a bound on the opaque type's associated type, and
// that bound is what the caller has to go on: the hidden type is not visible, so
// nothing else says what the projection can do. Bounds on such a projection were
// read off the trait's declaration of the item alone, so the one the opaque
// carries never took part, and both the method call and the trait obligation on
// `def().mk()` were reported as unsatisfiable.

trait Tr1 {
    type As1;

    fn mk(self) -> Self::As1;
}

#[derive(Clone, Copy)]
struct S2;

struct S1;

impl Tr1 for S1 {
    type As1 = S2;

    fn mk(self) -> Self::As1 {
        S2
    }
}

fn assert_copy<T: Copy>(x: T) -> (T, T) {
    (x, x)
}

fn def() -> impl Tr1<As1: Clone + Copy> {
    S1
}

fn main() {
    let _cloned = def().mk().clone();
    let (_left, _right) = assert_copy(def().mk());
}
