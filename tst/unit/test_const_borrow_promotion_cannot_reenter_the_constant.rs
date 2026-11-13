//@ compile-fail: error:0: cycle detected when evaluating constant

const ROOT: usize = read_root();

const fn read_root() -> usize {
    let borrowed = &ROOT;
    *borrowed
}

fn main() {}
