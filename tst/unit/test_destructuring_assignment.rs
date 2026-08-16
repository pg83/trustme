// Destructuring assignment (RFC 2909) writes a pattern on the left of `=`. The
// tuple form worked; the tuple-struct form, the slice form and a `..` rest in
// either of them did not.
//
// `(..)` and `..` are the same node once the parser drops parentheses, so the
// parenthesised one is marked: a bare `..` is the enclosing pattern's rest,
// while `(..)` is a sub-pattern of its own.
//
// Same shape as the upstream tests destructuring-assignment/tuple_destructure.rs,
// slice_destructure.rs and tuple_struct_destructure.rs.
struct TupleStruct<S, T>(S, T);

enum Enum<S, T> {
    SingleVariant(S, T),
}

fn main() {
    let (mut a, mut b);

    // Tuple-struct and enum-variant patterns.
    TupleStruct(a, b) = TupleStruct(0, 1);
    assert_eq!((a, b), (0, 1));

    Enum::SingleVariant(a, b) = Enum::SingleVariant(2, 3);
    assert_eq!((a, b), (2, 3));

    TupleStruct(a, .., b) = TupleStruct(4, 5);
    assert_eq!((a, b), (4, 5));

    TupleStruct(_, a) = TupleStruct(6, 7);
    assert_eq!(a, 7);

    TupleStruct(..) = TupleStruct(8, 9);
    assert_eq!(a, 7);

    // Slice patterns, with and without a rest.
    let mut c;
    [a, b] = [10, 11];
    assert_eq!((a, b), (10, 11));

    [a, .., b, c] = [12, 13, 14, 15, 16];
    assert_eq!((a, b, c), (12, 15, 16));

    [c, ..] = [17, 18, 19];
    assert_eq!(c, 17);

    [..] = [20, 21];
    assert_eq!(c, 17);

    // A bare `..` is the outer rest; a parenthesised one is a sub-pattern.
    (..) = (22, 23);
    assert_eq!(c, 17);

    ((a, .., b), .., (..)) = ((24, 25), ());
    assert_eq!((a, b), (24, 25));

    // Nested through a tuple struct.
    let mut d;
    TupleStruct([a, b], (c, d)) = TupleStruct([26, 27], (28, 29));
    assert_eq!((a, b, c, d), (26, 27, 28, 29));
}
