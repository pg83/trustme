// Extracted from src/const_eval.md:130
#![allow(unused)]
fn main() {
    // Even though the borrow is mutable and the temporary lives to the
      // end of the program due to promotion, this is allowed because the
      // borrow is not in tail position and so the scope of the temporary
      // is not extended via temporary lifetime extension.
      const C: () = { let _: &'static mut [u8] = &mut []; }; // OK
      //                                              ~~
      //                                     Promoted temporary.
}
