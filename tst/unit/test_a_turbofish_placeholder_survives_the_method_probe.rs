// munge_macro (under generic-array's tests) calls `.unzip::<_, _, Vec<_>,
// (Vec<_>, Vec<_>)>()` on a map whose closure returns `(u8, (u16, u32))`. The
// `_` written for `B` is the caller's own variable. While the method was
// probed, `FromB: Extend<B>` with the tuple impl `Extend<(A, B)> for (EA, EB)`
// read it as a tuple of the probe's own variables; the pick then handed back
// a fresh variable in its place - as for an impl parameter made for the probe
// - and nothing tied it to the `_` again: "Failed to infer type". Upstream
// instantiates only the method's unspecified parameters afresh; one written
// at the call is the caller's variable throughout.
fn main() {
    let rows = vec![(1u8, 2u16, 5u32), (3, 4, 6)];
    let (a, (b, c)) = rows.iter().map(|r| (r.0, (r.1, r.2))).unzip::<_, _, Vec<_>, (Vec<_>, Vec<_>)>();
    assert_eq!(a, [1, 3]);
    assert_eq!(b, [2, 4]);
    assert_eq!(c, [5, 6]);
}
