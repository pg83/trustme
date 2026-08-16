// The parser cannot tell a type argument from a const argument, so `_` in a
// path's generic list arrives as a type argument. When the callee takes a
// const parameter there instead, it has to move across: `make_buf::<_>()` used
// to be rejected with "Too many type parameters passed to make_buf".
//
// Same shape as the Rust Reference example items/generics.md:155.
fn make_buf<const N: usize>() -> [u8; N] {
    [0; _]
}

fn tagged<T, const N: usize>(value: T) -> ([T; N], usize)
where
    T: Copy,
{
    ([value; N], N)
}

struct Holder<const N: usize>;

fn main() {
    let buf: [u8; 1024] = make_buf::<_>();
    assert_eq!(buf.len(), 1024);

    // A type argument given explicitly, the const one inferred.
    let (values, count): ([u16; 3], usize) = tagged::<u16, _>(7);
    assert_eq!(count, 3);
    assert_eq!(values, [7, 7, 7]);

    // The same placeholder in type position, and parenthesised: the parser
    // drops the parens, so both spellings must land in the value list.
    let _: Holder<_> = Holder::<3>;
    let _: Holder<(((_)))> = Holder::<4>;
}
