// A trait object hides the type it was made from, so upstream assembles no
// structural auto-trait candidate for it: `dyn Debug` is `Send` only when
// written `dyn Debug + Send`. futures' auto_traits asserts that
// `Pin<Box<dyn Future>>` is not `Sync` through static_assertions.
//@ compile-fail: Failed to find an impl
fn need<T: ?Sized + Send>() {}

fn main() {
    need::<dyn std::fmt::Debug + Send>();
    need::<dyn std::fmt::Debug>();
}
