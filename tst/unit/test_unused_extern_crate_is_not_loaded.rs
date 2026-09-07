//@ run-pass
//@ aux-build: partial_eq_value_for_usize.rs
/* zerocopy's own tests (`test_try_cast_into_explicit_count`): `if let Some(expect) = None
   { .. assert_eq!(<a usize>, expect) }` leaves `?T` to `usize: PartialEq<?T>`, which the one
   impl `PartialEq<usize> for usize` decides - but only because serde_json, in the test's
   `--extern` table behind a dev-dependency the crate never names, is never loaded.
   Upstream loads an `--extern` crate on the first use of its name
   (`maybe_process_path_extern`), and the dependencies of each loaded crate with it; a crate
   never named is not loaded at all, and its `impl PartialEq<Value> for usize` is not seen.
   Loading every crate of the table made the goal ambiguous. */
fn meta() -> usize {
    8
}

fn main() {
    if let Some(expect) = None {
        assert_eq!(meta(), expect);
    }
}
