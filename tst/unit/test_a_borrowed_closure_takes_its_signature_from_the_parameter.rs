// pretty 0.12: `append_docs(self, &mut |doc| { f.entry(doc); })` with
// `consumer: &mut impl FnMut(&'d Doc)`. The borrow's operand expects the
// parameter's pointee, so the closure takes its signature from the
// parameter's bound before its body coerces `doc` to `&dyn Debug`.
use std::fmt::Debug;

fn walk<'d, T: Debug>(items: &'d [T], consumer: &mut impl FnMut(&'d T)) {
    for item in items {
        consumer(item);
    }
}

fn show(value: &dyn Debug, out: &mut Vec<String>) {
    out.push(format!("{:?}", value));
}

fn main() {
    let values = vec![1u8, 2];
    let mut out = Vec::new();
    walk(&values, &mut |value| {
        show(value, &mut out);
    });
    assert_eq!(out, ["1", "2"]);
}
