// `S = S;` names a unit struct on the left: such a pattern matches and holds
// nothing, so the assignment stores nothing and the program still runs.
struct S;

enum E {
    V,
}

type A = E;

fn main() {
    let mut a;

    S = S;
    (S, a) = (S, 7u8);
    assert_eq!(a, 7);

    E::V = E::V;
    (E::V, a) = (E::V, 8u8);
    assert_eq!(a, 8);

    <E>::V = E::V;
    A::V = A::V;
}
