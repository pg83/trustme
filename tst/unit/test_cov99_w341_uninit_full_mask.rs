//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: Invalid read:

union PartialWord {
    byte: u8,
    word: u64,
}

const BAD: u64 = {
    let value = PartialWord { byte: 1 };
    unsafe { value.word & 1 }
};

fn main() {}
