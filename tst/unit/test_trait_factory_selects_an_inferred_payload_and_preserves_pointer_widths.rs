trait Construct<T> {
    fn create(value: T) -> Self;
}

trait Decoy {
    fn create(value: u8) -> Self;
}

struct Other;
struct Payload<T>(T);

impl Decoy for Other {
    fn create(_: u8) -> Self {
        Other
    }
}

impl<T> Construct<T> for Payload<T> {
    fn create(value: T) -> Self {
        Payload(value)
    }
}

fn pointer_bytes<T: ?Sized>() -> usize {
    core::mem::size_of::<*const T>()
}

fn main() {
    let payload = Payload::<(_, _)>::create((71u16, [73u8, 79]));
    assert_eq!(payload.0, (71, [73, 79]));
    assert_eq!(pointer_bytes::<u32>(), core::mem::size_of::<usize>());
    assert_eq!(pointer_bytes::<[u16]>(), 2 * core::mem::size_of::<usize>());
}
