#![allow(dead_code, unused_assignments)]

trait Advance {
    type Next;
    fn advance(self) -> Self::Next;
}

#[derive(Copy, Clone)]
struct End(u8);
impl Advance for End {
    type Next = End;
    fn advance(self) -> End { self }
}

struct Other;
impl Advance for Other {
    type Next = Other;
    fn advance(self) -> Other { self }
}

fn advance<T: Advance>(value: T) -> T::Next {
    value.advance()
}

macro_rules! step {
    ($slot:ident) => {
        let target = &mut $slot;
        let mut $slot = None;
        *target = Some(advance($slot.unwrap()));
    };
}

macro_rules! chain {
    ($slot:ident;) => { step!($slot); };
    ($slot:ident; @ $($tail:tt)*) => {
        chain!($slot; $($tail)*);
        chain!($slot; $($tail)*);
    };
}

fn typecheck_backwards() {
    let mut slot = None;
    let head = advance(slot.unwrap());
    chain!(slot; @ @ @ @ @);
    slot = Some(End(7));
    let _: End = head;
}

fn main() {
    assert_eq!(advance(End(11)).0, 11);
}
