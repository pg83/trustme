//@ crate-type: lib
//@ compile-fail: trait bound

trait Marker {}
trait Bound<const N: usize> {}
trait Outer { type Assoc; }
struct Need<T: Marker>(T);

fn nested_const() -> Need<impl Outer<Assoc: Bound<_>>> {
    loop {}
}
