// lalrpop hands one closure to another inside a generic function. The
// argument's type is the first closure, whose type names the function's `V`;
// rustc's closures carry all of their parent's generics, while ours take those
// their types reach, and `V` was reached only through the argument's closure
// type, after the closure's parameters were already fixed.
fn outer<A, B, V: Default>() -> V {
    let make = || V::default();
    let apply = |g| {
        let _ = &g;
    };
    apply(make);
    V::default()
}

fn main() {
    assert_eq!(outer::<u8, u16, u64>(), 0);
}
