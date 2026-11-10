#![feature(negative_impls)]
//@ compile-fail: Failed to find an impl of

struct Ticket<T: Copy>(T);

impl<T> !Send for Ticket<T>
where
    T: Copy,
{}

fn require_send<T: Send>(_: T) {}

fn main() {
    require_send(Ticket(13u16));
}
