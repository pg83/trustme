#![feature(unboxed_closures)]

fn ordered<F>(call: F) -> <F as FnOnce<(u16,)>>::Output
where
    <F as FnOnce<(u16,)>>::Output: Ord,
    F: FnOnce<(u16,)>,
{
    call(83u16)
}

fn main() {
    let first = ordered(|value| value + 2u16);
    let second = ordered(|value| Some(value + 4u16));
    assert_eq!(first, 85u16);
    assert_eq!(second, Some(87u16));
}
