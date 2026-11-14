struct Payload(u32);

fn main() {
    let payload = Payload(42);
    let read = || &payload;
    let copied = read;
    assert_eq!(copied().0, 42);
    assert_eq!(read().0, 42);
}
