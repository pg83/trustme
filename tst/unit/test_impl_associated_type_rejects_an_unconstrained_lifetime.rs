//@ compile-fail: is not constrained by the impl trait
trait View { type Output; }
struct Record;
impl<'a> View for Record { type Output = &'a [u16]; }
fn main() {}
