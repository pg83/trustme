//@ compile-fail: Couldn't find path component 'absent'
mod outer {}
fn main() { let _ = crate::outer::absent::value; }
