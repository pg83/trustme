//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: Invalid read:

union PartialWord {
    byte: bool,
    word: u64,
}

const BAD: u64 = {
    let value = PartialWord { byte: true };
    unsafe { value.word & 1 }
};

fn main() {}
