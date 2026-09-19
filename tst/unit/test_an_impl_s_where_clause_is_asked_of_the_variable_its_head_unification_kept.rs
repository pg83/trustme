/* rand's `Uniform::new_inclusive(1, 4)` whose sample reaches a `usize` addition, as
   base64's `encoded.resize(encoded.len() + m, b'=')` does.  The literal is the
   argument of a parameter `B: SampleBorrow<X>`, and the only impl that can hold of an
   integer literal's variable is `impl<Borrowed: SampleUniform> SampleBorrow<Borrowed>
   for Borrowed` - `for &Borrowed` cannot, a number is no reference.  Proving it ties
   that variable to `X`, and `usize: Add<X>` then has only `impl Add<usize> for usize`
   to offer, so `X` is `usize`.

   Upstream proves an impl's where-clause against what its head unification bound, in
   the same inference context: `consider_impl_candidate`
   (rustc_next_trait_solver/src/solve/trait_goals.rs) runs
   `eq(goal.predicate.trait_ref, impl_trait_ref)` and only then instantiates
   `predicates_of(impl_def_id)` with those same args.  `?{integer}: SampleUniform` is
   ambiguous there - an integer variable unifies with every integer impl's self type,
   so several candidates remain - and the one `SampleBorrow` candidate stays viable.
   An integer variable nothing has constrained is defaulted to `i32` only afterwards,
   by `type_inference_fallback` (rustc_hir_typeck/src/fallback.rs), which selects the
   pending obligations first.

   Here the head unification runs in a table of its own and its bindings are
   materialized back afterwards.  Merging the goal's two variables gave the survivor
   the class the merged one carried, so the node that named the probe no longer
   matched it, the probe's own variable escaped into the caller's table - where its
   index means an unrelated type - and the where-clause was asked of that.  It had no
   solution, the one candidate died, `X` was never tied to the literal, and the
   literal was defaulted to `i32` on its own: `usize + i32`. */

trait SampleUniform: Sized {
    type Sampler;
}
impl SampleUniform for i32 {
    type Sampler = i32;
}
impl SampleUniform for usize {
    type Sampler = usize;
}
impl SampleUniform for u64 {
    type Sampler = u64;
}

trait SampleBorrow<Borrowed> {
    fn borrow(&self) -> &Borrowed;
}
impl<Borrowed: SampleUniform> SampleBorrow<Borrowed> for Borrowed {
    fn borrow(&self) -> &Borrowed {
        self
    }
}
impl<Borrowed: SampleUniform> SampleBorrow<Borrowed> for &Borrowed {
    fn borrow(&self) -> &Borrowed {
        self
    }
}

fn new_inclusive<X: SampleUniform + Copy, B: SampleBorrow<X>>(low: B) -> X {
    *low.borrow()
}

fn main() {
    let m = new_inclusive(1);
    let n: usize = 1usize + m;
    if n != 2 {
        panic!("sample did not come back as a usize");
    }

    /* The same parameter reached through a reference picks the `for &Borrowed` impl,
       so it is the pointee that names `X`. */
    let low: usize = 3;
    let k = new_inclusive(&low);
    if n + k != 5 {
        panic!("borrowed sample did not come back as a usize");
    }
}
