//@ edition: 2015
// proptest 0.8.7 (datafrog's dev-dependency) re-exports through
//     macro_rules! multiplex_alloc { ($($alloc: path, $std: path),*) =>
//         { $(pub(crate) use $std;)* } }
//     multiplex_alloc! { .., alloc::collections::VecDeque, std::collections::VecDeque, .. }
// inside its `std_facade` module. In edition 2015 a `use` path without a
// leading `::` is relative to the crate root, a path fragment included -
// `std` is the root's `extern crate std`. It was resolved against the module
// the `use` is in, as a 2018 path would be ("Cannot find component 1 of
// crate::std_facade::std::..").
mod facade {
    macro_rules! multiplex {
        ($($p:path),*) => { $(pub(crate) use $p;)* };
    }
    multiplex! { std::collections::VecDeque, std::vec }
}
fn main() {
    let mut v: facade::VecDeque<u8> = facade::VecDeque::new();
    v.push_back(1);
    let w: facade::vec::Vec<u8> = facade::vec::Vec::new();
    assert_eq!(v.len() + w.len(), 1);
}
