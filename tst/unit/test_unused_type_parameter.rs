//@ compile-fail: is never used in this struct
// A type parameter no field mentions cannot be inferred at a use site. It is
// allowed only when a bound ties it to a parameter that is used, or when a
// `PhantomData` field carries it.
//
// Same shape as the Rust Reference example items/generics.md:199.
struct Unused<T>;

fn main() {
    let _ = Unused::<u8>;
}
