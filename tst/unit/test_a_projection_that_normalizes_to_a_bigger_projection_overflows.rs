//@ compile-fail: overflow normalizing the associated type
/* A projection whose normal form is a bigger projection has no normal form at
   all: `<W<T> as Grow>::Out` is `<W<W<T>> as Grow>::Out`, which is
   `<W<W<W<T>>> as Grow>::Out`, and so on.  Nothing repeats, so no cycle check
   ends it - upstream ends it by depth, and so must we, rather than spinning
   forever.  `project` (rustc_trait_selection/src/traits/project.rs) refuses a
   projection obligation whose `recursion_depth` has left
   `tcx.recursion_limit()`, and `AssocTypeNormalizer`
   (rustc_trait_selection/src/traits/normalize.rs) raises
   `OverflowCause::DeeplyNormalize` on the same limit; the limit is the crate's
   `#![recursion_limit]`, 128 when the crate names none (`get_recursion_limit`,
   rustc_interface/src/limits.rs), and the diagnostic offers twice it
   (`suggest_new_overflow_limit`).  Upstream rejects this crate with E0275 too;
   the limit is named here only to keep the test quick. */
#![recursion_limit = "16"]

struct W<T>(T);

trait Grow {
    type Out;
}

impl<T> Grow for W<T>
where
    W<W<T>>: Grow,
{
    type Out = <W<W<T>> as Grow>::Out;
}

fn identity(value: <W<u8> as Grow>::Out) -> <W<u8> as Grow>::Out {
    value
}

fn main() {
    let _ = identity;
}
