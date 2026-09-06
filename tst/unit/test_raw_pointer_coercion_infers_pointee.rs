//@ run-pass
// `s.as_ref()` yields `&?U`, and the return type asks for `*const str`.
// Upstream tries unsizing first: `?U: Unsize<str>` is ambiguous with `?U`
// unknown and `str` no trait object, which it takes as "no unsizing", so
// `coerce_raw_ptr` unifies `*const ?U` with `*const str` - and that is what
// picks `AsRef<str>` among String's `AsRef` impls.
fn view(s: &String) -> *const str {
    s.as_ref()
}

fn borrow(s: &String) -> &[u8] {
    s.as_ref()
}

fn main() {
    let s = String::from("abc123");
    assert_eq!(unsafe { &*view(&s) }, "abc123");
    assert_eq!(borrow(&s), b"abc123");
}
