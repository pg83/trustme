//@ compile-fail: error
trait Width { const VALUE: usize; }
struct Missing;
struct Buffer([u8; <Missing as Width>::VALUE]);
fn main() {}
