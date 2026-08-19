//@ run-pass
// The follow-set rule is about what could continue the fragment, not about
// every neighbour: a closing delimiter ends the run it closes, a separator
// stands between iterations rather than after the last one, and a visibility
// is followed by the word that names the item it introduces.

macro_rules! ends_a_group {
    ({ $e:expr } $(, $t:ty)*) => {
        $e
    };
}

macro_rules! repeats {
    ($($e:expr),* ; $($t:ty),+) => {
        0 $(+ $e)*
    };
}

macro_rules! declares {
    ($v:vis struct $n:ident; $v2:vis fn $f:ident()) => {
        $v struct $n;
        $v2 fn $f() {}
    };
}

macro_rules! after_repetition {
    ($($e:expr),* ; $t:ty) => {
        0 $(+ $e)*
    };
}

macro_rules! typed {
    ($t:ty = $e:expr, $p:path as $u:ty) => {
        0
    };
}

declares!(pub struct Named; pub(crate) fn made());

fn main() {
    assert_eq!(ends_a_group!({ 1 + 2 }), 3);
    assert_eq!(repeats!(1, 2, 3; u8, u16), 6);
    assert_eq!(after_repetition!(1, 2; u8), 3);
    assert_eq!(typed!(u8 = 1, core::option::Option<u8> as u16), 0);
    let _ = Named;
    made();
}
