//@ compile-fail: generic
trait Pattern { const VALUE: Self; }
fn matches<T: Pattern>(value: T) -> bool {
    match value { T::VALUE => true, _ => false }
}
impl Pattern for u32 { const VALUE: Self = 13; }
fn main() { let _ = matches(13u32); }
