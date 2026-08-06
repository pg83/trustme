// Extracted from src/types/closure.md:831
#![allow(unused)]
fn main() {
    {
        let tuple =
          (String::from("foo"), String::from("bar"));
        {
            let c = || { // --------------------------+
                // tuple is captured into the closure |
                drop(tuple.0); //                     |
            }; //                                     |
        } // 'c' and 'tuple' dropped here ------------+
    }
}
