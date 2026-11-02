//@ compile-variant: -O
#[derive(Debug, PartialEq, Eq)]
struct Ticket<T>(T);
#[derive(Debug, PartialEq, Eq)]
struct Empty();
struct Marker;
const MAKE: fn(u8) -> Ticket<u8> = Ticket;
fn invoke<F: FnOnce(u8) -> Ticket<u8>>(function: F) -> Ticket<u8> { function(19) }
fn main() {
    let pointer: fn() -> Empty = Empty;
    assert_eq!(invoke(Ticket), Ticket(19));
    assert_eq!(MAKE(7), Ticket(7));
    assert_eq!(pointer(), Empty());
    assert_eq!(std::mem::size_of_val(&Ticket::<u8>), 0);
    assert_eq!(std::mem::size_of_val(&Marker), 0);
    assert_eq!(std::any::TypeId::of::<Marker>(), std::any::Any::type_id(&Marker));
}
