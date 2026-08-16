// A `where` clause needs no generic parameters to bind: a bound on a concrete
// type is trivially true or false, and Rust accepts it. Enums and unions only
// looked for the clause after a `<...>` list, so a generic-free one was an
// unexpected token where the body was expected.
//
// Same shape as the upstream tests layout/trivial-bounds-sized.rs and
// trivial-bounds/trivial-bounds-inconsistent.rs.
struct Struct
where
    u8: Copy,
{
    a: u8,
}

enum Enum
where
    u8: Copy,
    i16: Copy,
{
    A,
    B(u8),
}

union Union
where
    u8: Copy,
{
    a: u8,
    b: i8,
}

// The clause still works alongside real parameters.
enum Both<T>
where
    T: Copy,
    u8: Copy,
{
    Val(T),
}

fn main() {
    let s = Struct { a: 1 };
    assert_eq!(s.a, 1);

    let e = Enum::B(2);
    assert!(matches!(e, Enum::B(2)));
    assert!(matches!(Enum::A, Enum::A));

    let u = Union { a: 3 };
    assert_eq!(unsafe { u.a }, 3);
    assert_eq!(unsafe { u.b }, 3);

    let b: Both<u32> = Both::Val(4);
    let Both::Val(v) = b;
    assert_eq!(v, 4);
}
