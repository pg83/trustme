// Extracted from src/destructors.md:430
#![allow(unused)]
fn main() {
    use core::sync::atomic::{AtomicU64, Ordering::Relaxed};
      static X: AtomicU64 = AtomicU64::new(0);
      struct W<T>(T);
      impl<T> Drop for W<T> { fn drop(&mut self) { X.fetch_add(1, Relaxed); } }
      let W { 0: ref x } = W(()); // Struct pattern.
      x;
      let W(ref x) = W(()); // Tuple struct pattern.
      x;
      let (W(ref x),) = (W(()),); // Tuple pattern.
      x;
      let [W(ref x), ..] = [W(())]; // Slice pattern.
      x;
      let (Ok(W(ref x)) | Err(&ref x)) = Ok(W(())); // Or pattern.
      x;
      //
      // All of the temporaries above are still live here.
      assert_eq!(0, X.load(Relaxed));
}
