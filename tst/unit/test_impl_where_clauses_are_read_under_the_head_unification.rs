//@ run-pass
/* combine-4.6.4 `lib.rs:885`, `sep_by::<Vec<_>, _, _, _>(string("abc"), char(','))`:
   the collection parameter is pinned only by `F: Extend<P::Output>`, which here is
   `Vec<_>: Extend<&str>`.

   `Vec` has two `Extend` impls - `impl<T, A: Allocator> Extend<T> for Vec<T, A>` and
   `impl<'a, T: Copy + 'a, A: Allocator> Extend<&'a T> for Vec<T, A>` (alloc/src/vec/mod.rs).
   Upstream proves an impl candidate's where-clauses against the arguments its own trait
   reference was unified with: `consider_impl_candidate` (rustc_next_trait_solver,
   solve/trait_goals.rs) runs `eq(goal.predicate.trait_ref, impl_trait_ref)` and only then
   instantiates `predicates_of(impl_def_id)` with the same args, inside that one probe -
   as does the old selector, whose `match_impl` precedes `impl_obligations`.  That `eq`
   binds the goal's own inference variables, so the second impl arrives with `T = str` and
   dies on `str: Sized`; one candidate is left and the element type is `&str`.

   Reading the clauses off the unsubstituted head instead only ever asked `_: Sized` and
   `_: Copy`, both merely ambiguous, so both impls stayed viable and the goal was reported
   as `type annotations needed: cannot infer a type satisfying Vec<_>: Extend<&str>`. */

fn collect_one<F: Extend<T> + Default, T>(item: T) -> F {
    let mut collection = F::default();
    collection.extend(Some(item));
    collection
}

fn main() {
    /* The rejected impl needs `str: Sized`. */
    let borrowed: Vec<_> = collect_one("abc");
    assert_eq!(borrowed.len(), 1);
    assert_eq!(borrowed[0], "abc");

    /* The rejected impl needs `String: Copy`: sized, but not copy. */
    let owner = String::from("xyz");
    let referenced: Vec<_> = collect_one(&owner);
    assert_eq!(referenced.len(), 1);
    assert_eq!(*referenced[0], *"xyz");

    /* The impl that is rejected for `&str` is the one that applies here. */
    let copied: Vec<u8> = collect_one(&7u8);
    assert_eq!(copied, [7u8]);
}
