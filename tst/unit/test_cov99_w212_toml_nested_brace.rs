//@ compile-flags: --target $DIR/test_cov99_w212_toml_nested_brace.toml

fn main() {
    assert!(cfg!(target_arch = "x86_64"));
    assert_eq!(std::mem::size_of::<usize>(), 8);
}
