// `--extern name` with no path names a crate to load from the search
// directories; only `name=path` says where it is.
//@ edition: 2018
//@ compile-flags: --extern alloc

fn main() {
    let mut v: alloc::vec::Vec<i32> = alloc::vec::Vec::new();
    v.push(3);
    assert_eq!(v.len(), 1);
}
