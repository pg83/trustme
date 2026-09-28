// libc's `#![crate_name = "libc"]` in its `--test` build: the attribute renamed
// the crate while expansion still took it for a library, so `$crate` in a local
// macro named `libc-<tag>`, a crate the finished binary does not load. rustc
// settles the crate name and types from the crate's own attributes before
// expansion (`find_crate_name`, `collect_crate_types`): `--test` or a command
// line type wins over `#![crate_type]`, `--crate-name` over `#![crate_name]`.
#![crate_name = "named"]

const ANSWER: u32 = 42;

macro_rules! answer {
    () => {
        $crate::ANSWER
    };
}

fn main() {
    assert_eq!(answer!(), 42);
}
