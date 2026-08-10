// Extracted from library/core/src/cmp.rs:1031
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

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

    assert_eq!(Equal("self").min(Equal("other")).0, "self");
}
