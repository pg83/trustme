struct Ticket(u32);

unsafe impl Send for Ticket {}

fn require_send<T: Send>(value: T) -> T {
    value
}

fn main() {
    assert_eq!(require_send(Ticket(593)).0, 593);
}
