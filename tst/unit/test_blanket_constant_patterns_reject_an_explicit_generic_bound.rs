//@ compile-fail: generic parameters
trait Pattern { const VALUE: u8; }
impl<T> Pattern for T { const VALUE: u8 = 17; }
fn implicit<T>(value: u8) -> bool {
    match value { <T as Pattern>::VALUE => true, _ => false }
}
fn explicit<T: Pattern>(value: u8) -> bool {
    match value { T::VALUE => true, _ => false }
}
fn main() {
    for value in [0u8, 13, 17, 23] {
        assert_eq!(implicit::<String>(value), value == 17);
        assert_eq!(explicit::<u32>(value), value == 17);
    }
}
