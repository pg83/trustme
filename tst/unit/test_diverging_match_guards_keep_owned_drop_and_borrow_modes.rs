use std::cell::Cell;
struct Ticket<'a> { drops: &'a Cell<u8>, value: u8 }
impl Drop for Ticket<'_> { fn drop(&mut self) { self.drops.set(self.drops.get() + 1); } }
fn moved(input: (u8, Ticket<'_>)) -> u8 {
    match input { (1, ticket) | (2, ticket) if { return ticket.value; } => 99, _ => 0 }
}
fn shared(input: (u8, Ticket<'_>)) -> u8 {
    match input { (1, ref ticket) | (2, ref ticket) if { return ticket.value + 1; } => 99, _ => 0 }
}
fn unique(mut input: (u8, Ticket<'_>)) -> u8 {
    match input { (1, ref mut ticket) | (2, ref mut ticket) if { return ticket.value + 2; } => 99, _ => 0 }
}
fn main() {
    let drops = Cell::new(0);
    assert_eq!(moved((2, Ticket { drops: &drops, value: 17 })), 17);
    assert_eq!(shared((1, Ticket { drops: &drops, value: 23 })), 24);
    assert_eq!(unique((2, Ticket { drops: &drops, value: 37 })), 39);
    assert_eq!(unique((0, Ticket { drops: &drops, value: 41 })), 0);
    assert_eq!(drops.get(), 4);
}
