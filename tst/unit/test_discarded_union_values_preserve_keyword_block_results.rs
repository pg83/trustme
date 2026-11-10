#![warn(unused_must_use)]

use core::cell::Cell;

#[must_use]
union Ticket {
    value: u16,
}

fn ticket(calls: &Cell<u16>) -> Ticket {
    calls.set(calls.get() + 1);
    Ticket { value: 17 }
}

fn main() {
    let calls = Cell::new(0);
    let result = 'done: {
        let item = ticket(&calls);
        let value = unsafe { item.value };
        break 'done value + const { 5u16 };
    };
    ticket(&calls);
    assert_eq!(result, 22);
    assert_eq!(calls.get(), 2);
}
