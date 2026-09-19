/* A chain of projections that does converge has to keep compiling: the depth
   limit on normalizing a projection (see
   `TraitResolution::expandAssociatedTypesInplaceUfcsKnown`) is the crate's
   `#![recursion_limit]`, 128 unless the crate names one - the same limit
   upstream gives `AssocTypeNormalizer` and `project`
   (rustc_trait_selection/src/traits/normalize.rs, .../traits/project.rs,
   `get_recursion_limit` in rustc_interface/src/limits.rs).  Normalizing this
   crate's `<W<..<u8>..> as Grow>::Out` walks 40 projections down and each one
   is smaller than the last, so the limit is never reached. */

struct W<T>(T);

trait Grow {
    type Out;
}

impl Grow for u8 {
    type Out = u8;
}

impl<T: Grow> Grow for W<T> {
    type Out = W<<T as Grow>::Out>;
}

fn grow(value: W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<u8>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>) -> <W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<u8>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> as Grow>::Out {
    value
}

fn main() {
    let deep: W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<W<u8>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> = W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(W(0u8))))))))))))))))))))))))))))))))))))))));
    assert_eq!(core::mem::size_of_val(&grow(deep)), 1);
}
