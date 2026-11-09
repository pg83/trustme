#![feature(type_alias_impl_trait)]

type Ticket = impl Copy + PartialEq<u8>;

#[define_opaque(Ticket)]
fn issue() -> Ticket {
    43u8
}

fn main() {
    let ticket = issue();
    let copied = ticket;
    assert!(ticket == 43u8);
    assert!(copied == 43u8);
}
