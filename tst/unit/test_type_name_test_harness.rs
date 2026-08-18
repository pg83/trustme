// The test harness names the crate `X$test` so its symbols do not collide with
// the crate it is built from; the type name is the name the crate was written
// under.
//@ test-harness

struct NT;

struct Wrapper<T>(T);

#[test]
fn crate_name_has_no_harness_suffix() {
    assert_eq!(
        std::any::type_name::<NT>(),
        "test_type_name_test_harness::NT"
    );
    assert_eq!(
        std::any::type_name::<Wrapper<NT>>(),
        "test_type_name_test_harness::Wrapper<test_type_name_test_harness::NT>"
    );
}
