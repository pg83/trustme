//@ compile-fail: evaluation panicked during constant evaluation
//@ compile-flags: -Cpanic=abort
#![feature(never_type)]

struct Record {
    present: (),
}

const _: () = loop {
    let value: ! = panic!("loop never pattern");
    let Record { absent: _, .. }: ! = value;
};

fn main() {}
