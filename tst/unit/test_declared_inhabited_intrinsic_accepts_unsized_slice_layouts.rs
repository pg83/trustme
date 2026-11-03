#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
const fn assert_inhabited<T: ?Sized>();

enum Void {}
struct Tail<T: ?Sized> { prefix: u8, tail: T }

const CHECK: () = {
    assert_inhabited::<[u16]>();
    assert_inhabited::<[Void]>();
    assert_inhabited::<str>();
    assert_inhabited::<Tail<[Void]>>();
};

fn main() {
    let empty: [Void; 0] = [];
    let tail = Tail { prefix: 42, tail: empty };
    let view: &Tail<[Void]> = &tail;
    assert_eq!(view.prefix, 42);
    assert_eq!(view.tail.len(), 0);
    assert_eq!(CHECK, ());
}
