//@ compile-fail: Couldn't find constant name 'Absent'
fn main() { match Some(3u8) { std::option::Absent(_) => (), _ => () } }
