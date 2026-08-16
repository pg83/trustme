// `[operand; N]` requires the operand to be `Copy` only when it is a value
// expression. A path to a `const` item is exempt, and that exemption covers
// associated consts, not just free ones. `MaybeUninit<T>` is `Copy` only for
// `T: Copy`, so a repeat of `Self::ELEM` under an unbounded `T` used to be
// rejected with "Failed to find an impl of Copy".
//
// Same shape as rustc's check-pass ui test
// const-generics/generic_const_exprs/poly-const-uneval-ice-106423.rs, and as
// arrayvec's `MakeMaybeUninit::ARRAY`.
use core::mem::MaybeUninit;

struct NotCopy(u32);

const FREE: MaybeUninit<NotCopy> = MaybeUninit::uninit();

struct Arr<T, const N: usize> {
    v: [MaybeUninit<T>; N],
}

impl<T, const N: usize> Arr<T, N> {
    const ELEM: MaybeUninit<T> = MaybeUninit::uninit();
    const INIT: [MaybeUninit<T>; N] = [Self::ELEM; N];

    fn new() -> Self {
        Arr { v: Self::INIT }
    }
}

trait Empty: Sized {
    const EMPTY: MaybeUninit<Self>;
}

impl Empty for NotCopy {
    const EMPTY: MaybeUninit<NotCopy> = MaybeUninit::uninit();
}

fn via_trait<T: Empty, const N: usize>() -> [MaybeUninit<T>; N] {
    [T::EMPTY; N]
}

fn main() {
    // Inherent associated const: a UFCS path the front end only binds during
    // type checking.
    let inherent = Arr::<NotCopy, 4>::new();
    assert_eq!(inherent.v.len(), 4);

    // Trait associated const.
    let trait_const = via_trait::<NotCopy, 2>();
    assert_eq!(trait_const.len(), 2);

    // Free const item: the case that already worked.
    let free = [FREE; 3];
    assert_eq!(free.len(), 3);
}
