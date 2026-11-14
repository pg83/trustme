//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: Invalid read:

union PartialPair {
    first: bool,
    pair: (bool, bool),
}

const BAD: bool = {
    let value = PartialPair { first: true };
    unsafe { value.pair.1 & true }
};

fn main() {}
