// Which impl applies is decided by the receiver, and that impl is what fixes the
// trait's own arguments; the call's arguments are then checked against the
// signature it gives. Relating an argument first instead reads its type into a
// trait argument nothing had claimed - a function item names itself rather than
// the pointer it would become, so `Index<fn{bar}>` was looked for where the
// program wrote `Index<fn()>`. The same call written as `S[bar]` went through.

use std::ops::Index;

static UNIT: () = ();

struct S;

fn bar() {}

impl Index<fn()> for S {
    type Output = ();

    fn index(&self, _: fn()) -> &() {
        &UNIT
    }
}

fn main() {
    S.index(bar);
    S[bar];
}
