// Extracted from src/types/closure.md:752
#![allow(unused)]
fn main() {
    {
        let tuple =
          (String::from("foo"), String::from("bar")); // --+
        { //                                               |
            let c = || { // ----------------------------+  |
                // tuple.0 is captured into the closure |  |
                drop(tuple.0); //                       |  |
            }; //                                       |  |
        } // 'c' and 'tuple.0' dropped here ------------+  |
    } // tuple.1 dropped here -----------------------------+
}
