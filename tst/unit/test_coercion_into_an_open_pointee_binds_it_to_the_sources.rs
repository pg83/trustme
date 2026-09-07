//@ run-pass
/* zerocopy's `transmute_ref!` (its body copied) for a sized -> unsized transmutation.
   `let e: &_ = $e` and the arm `if false { t.transmute_ref_inference_helper() }` against
   the `if`'s `&[[u8; 2]]` are coercions of a reference into one whose pointee is open (or
   the other way round): upstream's `coerce_unsized` finds `Src: Unsize<?T>` ambiguous
   and gives up, and `coerce_borrowed_pointer` unifies the two pointees there and then -
   at a `let` or an `if` arm as much as at an argument.  So `e: &[u8; 8]` and `Dst =
   [[u8; 2]]` are known before `t.transmute_ref()` is looked up on `Wrap<&Src, &Dst>`,
   and the inherent `transmute_ref` (its `Dst` implicitly `Sized`) is rejected for the
   `TransmuteRefDst` one (`Dst: ?Sized`).  Deferring those pointees left the lookup to
   the fallback revisit with `Dst` open, where the inherent method was picked. */
use std::marker::PhantomData;

pub trait IntoBytes {}
pub trait FromBytes {}
pub trait Immutable {}
impl IntoBytes for [u8; 8] {}
impl Immutable for [u8; 8] {}
impl FromBytes for [[u8; 2]] {}
impl Immutable for [[u8; 2]] {}

pub struct Wrap<Src, Dst>(pub Src, pub PhantomData<Dst>);

impl<Src, Dst> Wrap<Src, Dst> {
    pub const fn new(src: Src) -> Self {
        Wrap(src, PhantomData)
    }
}

impl<'a, Src, Dst> Wrap<&'a Src, &'a Dst>
where
    Src: ?Sized,
    Dst: ?Sized,
{
    pub const fn transmute_ref_inference_helper(self) -> &'a Dst {
        loop {}
    }
}

impl<'a, Src, Dst> Wrap<&'a Src, &'a Dst> {
    pub const unsafe fn transmute_ref(self) -> &'a Dst {
        panic!("inherent")
    }
}

pub trait TransmuteRefDst<'a> {
    type Dst: ?Sized;
    fn transmute_ref(self) -> &'a Self::Dst;
}

impl<'a, Src: ?Sized, Dst: ?Sized> TransmuteRefDst<'a> for Wrap<&'a Src, &'a Dst>
where
    Src: IntoBytes + Immutable,
    Dst: FromBytes + Immutable,
{
    type Dst = Dst;
    fn transmute_ref(self) -> &'a Dst {
        panic!("trait")
    }
}

macro_rules! transmute_ref {
    ($e:expr) => {{
        let e: &_ = $e;
        #[allow(unused, clippy::diverging_sub_expression)]
        if false {
            struct AssertSrcIsIntoBytes<'a, T: ?::core::marker::Sized + IntoBytes>(&'a T);
            struct AssertSrcIsImmutable<'a, T: ?::core::marker::Sized + Immutable>(&'a T);
            struct AssertDstIsFromBytes<'a, U: ?::core::marker::Sized + FromBytes>(&'a U);
            struct AssertDstIsImmutable<'a, T: ?::core::marker::Sized + Immutable>(&'a T);
            let _ = AssertSrcIsIntoBytes(e);
            let _ = AssertSrcIsImmutable(e);
            if true {
                #[allow(unused, unreachable_code)]
                let u = AssertDstIsFromBytes(loop {});
                u.0
            } else {
                #[allow(unused, unreachable_code)]
                let u = AssertDstIsImmutable(loop {});
                u.0
            }
        } else {
            use TransmuteRefDst;
            let t = Wrap::new(e);
            if false {
                t.transmute_ref_inference_helper()
            } else {
                t.transmute_ref()
            }
        }
    }};
}

#[allow(dead_code)]
fn never_called() -> usize {
    let array_of_u8s = [0u8, 1, 2, 3, 4, 5, 6, 7];
    let x: &[[u8; 2]] = transmute_ref!(&array_of_u8s);
    x.len()
}

fn main() {
    assert!(std::env::args().count() < 1_000_000 || never_called() == 4);
}
