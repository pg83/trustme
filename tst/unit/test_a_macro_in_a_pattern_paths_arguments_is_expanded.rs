// async-trait matches `ReceiverKind::Reference(_, _, None::<Token![mut]>)`.
// Upstream's expansion walks every path, a pattern's included, and expands
// the macros in its generic arguments; ours left the paths of patterns
// alone and resolution met an unexpanded `Token![mut]`.
macro_rules! byte {
    () => { u8 };
}
fn kind(x: Option<u8>) -> u8 {
    match x {
        None::<byte![]> => 0,
        Some::<byte![]>(v) => v,
    }
}
fn main() {
    assert_eq!(kind(None), 0);
    assert_eq!(kind(Some(5)), 5);
    let Some::<byte![]>(w) = Some(7u8) else { panic!() };
    assert_eq!(w, 7);
}
