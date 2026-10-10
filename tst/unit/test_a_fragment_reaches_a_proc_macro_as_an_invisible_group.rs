//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// typewit's `inj_type_fn!` hands `$ty_arg:ty` first to its
// `__impl_with_span!` proc macro, which takes the type as the first token
// tree and expects the impl's attributes as the second, `()`-delimited.
// Upstream transcribes a fragment matched as `ty`, `expr`, `path`, `pat`,
// `block`, `stmt`, `meta`, `vis` or `item` as its tokens inside an invisible
// delimiter (`mk_delimited`, rustc_expand/src/mbe/transcribe.rs), and the
// proc-macro server hands that to the macro as a `Delimiter::None` group; an
// `ident` or a `tt` stays as it was written. We spelled a type out flat, so
// `Vec<u8>` was four trees and the second was `<`.
extern crate proc_macro_item_passthrough;
use proc_macro_item_passthrough::{echo, tree_shapes};

macro_rules! shapes {
    ($t:ty, $u:ty, $e:expr, $a:expr, $p:path, $q:pat, $b:block, $s:stmt, $m:meta, $v:vis, $it:item, $i:ident, $tt:tt) => {
        tree_shapes!($t $u $e $a $p $q $b $s $m $v $it $i $tt)
    };
}

macro_rules! forwarded {
    ($e:expr) => {
        forwarded_again!($e)
    };
}

macro_rules! forwarded_again {
    ($x:expr) => {
        tree_shapes!($x)
    };
}

macro_rules! forwarded_type {
    ($t:ty) => {
        forwarded_type_again!($t)
    };
}

macro_rules! forwarded_type_again {
    ($u:ty) => {
        tree_shapes!($u $u)
    };
}

macro_rules! size_through_a_proc_macro {
    ($t:ty) => {
        echo! { const SIZE: usize = core::mem::size_of::<$t>(); }
    };
}

size_through_a_proc_macro!([u16; 3]);

fn main() {
    assert_eq!(
        shapes!(Vec<u8>, u8, 1 + 2, 5, a::b, Some(_), { 1 }, let x = 1, inline(always), pub(crate), fn f() {}, x, (y)),
        "none none none none none none none none none none none ident paren"
    );
    assert_eq!(forwarded!(1 + 2), "none");
    assert_eq!(forwarded_type!(Vec<u8>), "none none");
    assert_eq!(SIZE, 6);
}
