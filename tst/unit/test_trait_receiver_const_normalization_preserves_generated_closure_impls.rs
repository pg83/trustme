#![feature(arbitrary_self_types, generic_const_exprs)]
#![allow(incomplete_features)]

trait Share<const N: usize> {
    type Ref;
}

impl<T: 'static> Share<0> for T {
    type Ref = &'static T;
}

const fn extent() -> usize {
    let callback = || 23u8;
    let _ = callback;
    0
}

trait Read: Sized + 'static {
    fn read(self: <Self as Share<{ extent() }>>::Ref) -> u8;
}

struct Record(u8);

impl Read for Record {
    fn read(self: &'static Self) -> u8 {
        self.0
    }
}

static RECORD: Record = Record(29);

fn main() {
    assert_eq!(<Record as Read>::read(&RECORD), 29);
}
