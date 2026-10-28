//@ compile-fail: Unknown enum variant 'Missing'
enum E { A }
use E::Missing;
fn main() {}
