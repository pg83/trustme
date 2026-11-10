//@ compile-flags: --test
//@ run-fail: 2 failed
//@ env: RUST_TEST_THREADS=1

#[test]
#[should_panic]
fn bare_marker_requires_a_panic() {}

#[test]
#[should_panic()]
fn empty_marker_requires_a_panic() {}

#[test]
fn ordinary_test_keeps_its_successful_result() {
    let value = std::hint::black_box(29u16);
    assert_eq!(value, 29);
}
