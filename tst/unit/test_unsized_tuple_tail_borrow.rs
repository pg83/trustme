// A tuple whose last element is unsized is itself unsized, so a reference to it
// carries metadata.
use std::fmt;

fn any<T>() -> T {
    unreachable!()
}

// Compiled but never run: an unsized tuple can only be made by transmuting.
#[allow(dead_code)]
fn debug_tail() {
    let pair: &(u8, dyn fmt::Debug) = any();
    let _ = format!("{:?}", &pair.1);
}

fn main() {}
