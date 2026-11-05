//@ aux-build: indexed_opaque_tuple_provider.rs
//@ compile-fail: error:

trait Same<R> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}

fn main() {
    let pair = indexed_opaque_tuple_provider::make();
    require(pair.0, pair.1);
}
