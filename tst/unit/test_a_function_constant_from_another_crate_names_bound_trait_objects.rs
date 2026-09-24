//@ aux-build: boxed_error_stream.rs
// A generic function from another crate arrives as MIR. Its call
// `self.0.next().map(Ok)` passes `Result::Ok::<_, Box<dyn Any + Send>>` as
// a function constant, and the bind pass left that constant's path alone:
// the trait object in it kept no trait, and codegen crashed looking for
// its vtable. futures' stream_catch_unwind reaches `CatchUnwind` so.
use boxed_error_stream::{Caught, Stream};

fn main() {
    let mut s = Caught(vec![10, 11].into_iter());
    assert_eq!(10, s.next().unwrap().ok().unwrap());
    assert_eq!(11, s.next().unwrap().ok().unwrap());
    assert!(s.next().is_none());
}
