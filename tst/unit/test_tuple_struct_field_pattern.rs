// A tuple struct can be matched with the braced form, naming its fields by
// index. A trailing `..` there leaves the remaining fields unmatched, but the
// pattern was still built as a complete one, so the field count disagreed and
// the compiler asserted.
//
// Same shape as the Rust Reference example patterns.md:723.
struct PointTuple(u32, u32, u32);

fn describe(t: &PointTuple) -> &'static str {
    match t {
        PointTuple { 0: 10, 1: 20, 2: 30 } => "all",
        PointTuple { 2: 30, 1: 20, 0: 11 } => "reordered",
        PointTuple { 0: 12, .. } => "first",
        PointTuple { 2: 31, .. } => "last",
        PointTuple { .. } => "any",
    }
}

fn main() {
    assert_eq!(describe(&PointTuple(10, 20, 30)), "all");
    assert_eq!(describe(&PointTuple(11, 20, 30)), "reordered");
    assert_eq!(describe(&PointTuple(12, 0, 0)), "first");
    assert_eq!(describe(&PointTuple(0, 0, 31)), "last");
    assert_eq!(describe(&PointTuple(1, 2, 3)), "any");

    // Bindings work through the same form.
    let PointTuple { 1: b, .. } = PointTuple(4, 5, 6);
    assert_eq!(b, 5);
}
