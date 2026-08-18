// A transmute keeps the size, so a zero-sized source has a zero-sized
// destination: there are no bytes to copy, and the source has no storage to
// copy them from.
//@ compile-flags: -C debug-assertions

#[allow(dead_code)]
enum Single {
    A,
}

struct Empty;

fn main() {
    let value: Single = unsafe { std::mem::transmute::<(), Single>(()) };
    assert!(matches!(value, Single::A));

    let _: Empty = unsafe { std::mem::transmute::<(), Empty>(()) };
    let _: () = unsafe { std::mem::transmute::<Empty, ()>(Empty) };
    let _: [u8; 0] = unsafe { std::mem::transmute::<(), [u8; 0]>(()) };
}
