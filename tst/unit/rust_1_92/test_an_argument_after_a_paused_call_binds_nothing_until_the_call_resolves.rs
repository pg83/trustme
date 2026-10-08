// sct builds ring 0.16, whose build script lists its libraries as
// `[("", &core[..], ..), ("test", &test[..], &[])]` and, in a closure over
// them, names each one with `String::from(prefix) + suffix` before passing
// `&lib_name` as a `&str`. rustc coerces each array element into the
// element type as it meets it, so `suffix` is a `&str` when the `+` is
// checked, and `lib_name` is a `String`. Here `libs.iter()` waited for the
// element coercions, and the coercion of `&lib_name` waited for the call;
// but the phase that binds an argument's open pointee did not wait, and
// bound `lib_name: str` before the `+` was resolved.
fn build(name: &str, srcs: &[&str]) -> usize {
    name.len() + srcs.len()
}

fn main() {
    let core = vec!["a.c", "b.c"];
    let libs = [("", &core[..]), ("test", &[])];
    let mut total = 0;
    libs.iter().for_each(|&(suffix, srcs)| {
        let lib_name = String::from("ring_core_") + suffix;
        total += build(&lib_name, srcs);
    });
    assert_eq!(total, 12 + 14);
}
