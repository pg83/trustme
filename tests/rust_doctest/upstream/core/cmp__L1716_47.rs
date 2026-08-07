// Extracted from library/core/src/cmp.rs:1716
#![allow(unused)]
#![feature(cmp_minmax)]
fn main() {
    use std::cmp::{self, Ordering};

    #[derive(Eq)]
    struct Equal(&'static str);

    impl PartialEq for Equal {
        fn eq(&self, other: &Self) -> bool { true }
    }
    impl PartialOrd for Equal {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> { Some(Ordering::Equal) }
    }
    impl Ord for Equal {
        fn cmp(&self, other: &Self) -> Ordering { Ordering::Equal }
    }

    assert_eq!(cmp::minmax(Equal("v1"), Equal("v2")).map(|v| v.0), ["v1", "v2"]);
}
