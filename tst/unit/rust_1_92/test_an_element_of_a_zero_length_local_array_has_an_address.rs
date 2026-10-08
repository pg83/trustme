// borsh's tests index arrays that turn out to have no elements, and the
// generated C++ named a local it never declared: "use of undeclared
// identifier 'var2'". A zero-sized local has no storage. rustc places it at
// a dangling address aligned for its type and computes a projection's
// address from there, so `&empty[i]` is well formed, and the bounds check
// panics before it is used. We leave zero-sized locals undeclared and
// handled a borrow of one only when the borrowed place was zero-sized too;
// an element of a `[u8; 0]` is a `u8`.
#[inline(never)]
fn element(i: usize) -> *const u8 {
    let empty: [u8; 0] = [];
    &empty[i] as *const u8
}

fn main() {
    let result = std::panic::catch_unwind(|| element(std::env::args().count() - 1));
    assert!(result.is_err());
}
