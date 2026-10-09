// constcat 0.6's smoke test: `use core::array as core;` names `core` in the
// block it is written in, but the path of that import is resolved without
// it, so `core` there is the crate.
fn main() {
    #[allow(unused_imports)]
    use core::array as core;

    let lanes: usize = core::from_fn::<u8, 3, _>(|i| i as u8).len();
    assert_eq!(lanes, 3);
}
