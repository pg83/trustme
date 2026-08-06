// Extracted from src/items/functions.md:312
#![allow(unused)]
fn main() {
    // Returns a future that, when awaited, dereferences `x`.
    //
    // Soundness condition: `x` must be safe to dereference until
    // the resulting future is complete.
    async unsafe fn unsafe_example(x: *const i32) -> i32 {
      *x
    }
    
    async fn safe_example() {
        // An `unsafe` block is required to invoke the function initially:
        let p = 22;
        let future = unsafe { unsafe_example(&p) };
    
        // But no `unsafe` block required here. This will
        // read the value of `p`:
        let q = future.await;
    }
}
