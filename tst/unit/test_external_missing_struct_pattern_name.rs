//@ compile-fail: Couldn't find pattern type 'Absent'
fn main() {
    match Some(3u8) { std::option::Absent { .. } => (), _ => () }
}
