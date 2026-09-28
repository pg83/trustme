// A tuple struct's or tuple variant's constructor used as a `fn` value is a
// Rust-ABI function like any other: a field larger than two pointers is
// passed by reference, as the `fn` pointer type says.
#[derive(Debug, PartialEq, Clone, Copy)]
struct Big(u64, u64, u64);

#[derive(Debug, PartialEq)]
enum Wrapped {
    Plain(Big),
    Pair(Big, u8),
}

#[derive(Debug, PartialEq)]
struct Holder(Big);

fn apply<T>(f: fn(Big) -> T, value: Big) -> T {
    f(value)
}

fn main() {
    let big = Big(1, 2, 3);
    assert_eq!(apply(Wrapped::Plain, big), Wrapped::Plain(big));
    assert_eq!(apply(Holder, big), Holder(big));
    let failed: Result<u8, Big> = Err(big);
    assert_eq!(failed.map_err(Wrapped::Plain), Err(Wrapped::Plain(big)));
    let pair: fn(Big, u8) -> Wrapped = Wrapped::Pair;
    assert_eq!(pair(big, 7), Wrapped::Pair(Big(1, 2, 3), 7));
    println!("ok");
}
