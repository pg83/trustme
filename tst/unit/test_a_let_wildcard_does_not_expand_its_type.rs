// generic-array's miri_stress test binds `let _ = LargeArray::from_array(..)`,
// a `GenericArray<(), U1 << 30>`: its storage type is a binary tree of
// `GenericArrayImplEven { parents: [U; 2], .. }` thirty levels deep. Upstream
// lowers a wildcard to no test and no subpattern, whatever the type. The
// pattern rules expanded `_` into one rule per leaf field - 2^30 of them -
// which match arms need, as their rules are compared column by column; a
// `let` has one arm, and one `_` rule stands for the whole value.
struct Node<T> {
    parents: [T; 2],
}

type T0 = ();
type T1 = Node<T0>;
type T2 = Node<T1>;
type T3 = Node<T2>;
type T4 = Node<T3>;
type T5 = Node<T4>;
type T6 = Node<T5>;
type T7 = Node<T6>;
type T8 = Node<T7>;
type T9 = Node<T8>;
type T10 = Node<T9>;
type T11 = Node<T10>;
type T12 = Node<T11>;
type T13 = Node<T12>;
type T14 = Node<T13>;
type T15 = Node<T14>;
type T16 = Node<T15>;
type T17 = Node<T16>;
type T18 = Node<T17>;
type T19 = Node<T18>;
type T20 = Node<T19>;
type T21 = Node<T20>;
type T22 = Node<T21>;
type T23 = Node<T22>;
type T24 = Node<T23>;
type T25 = Node<T24>;
type T26 = Node<T25>;
type T27 = Node<T26>;
type T28 = Node<T27>;
type T29 = Node<T28>;
type T30 = Node<T29>;
type T31 = Node<T30>;
type T32 = Node<T31>;
type T33 = Node<T32>;
type T34 = Node<T33>;
type T35 = Node<T34>;
type T36 = Node<T35>;
type T37 = Node<T36>;
type T38 = Node<T37>;
type T39 = Node<T38>;
type T40 = Node<T39>;

fn tree() -> T40 {
    unsafe { std::mem::transmute::<(), T40>(()) }
}

fn main() {
    let _ = tree();
    let t = tree();
    let _moved = t;
}
