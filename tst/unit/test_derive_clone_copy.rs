// A type that derives both `Copy` and `Clone` clones by copying. Cloning field
// by field instead calls each field's own `Clone` impl, which is observable
// when that impl does more than copy.
//
// Same shape as the upstream test deriving/deriving-copyclone.rs.
use std::sync::atomic::{AtomicUsize, Ordering};

static CLONED: AtomicUsize = AtomicUsize::new(0);

#[derive(Copy)]
struct Liar;

impl Clone for Liar {
    fn clone(&self) -> Self {
        CLONED.fetch_add(1, Ordering::SeqCst);
        *self
    }
}

#[derive(Copy, Clone)]
struct Innocent(#[allow(dead_code)] Liar);

// The two attributes may be written separately.
#[derive(Copy)]
#[derive(Clone)]
struct Split {
    #[allow(dead_code)]
    a: Liar,
}

#[derive(Copy, Clone)]
enum Choice {
    A(#[allow(dead_code)] Liar),
    B,
}

fn main() {
    let _ = Innocent(Liar).clone();
    assert_eq!(CLONED.load(Ordering::SeqCst), 0);

    let _ = Split { a: Liar }.clone();
    assert_eq!(CLONED.load(Ordering::SeqCst), 0);

    let _ = Choice::A(Liar).clone();
    let _ = Choice::B.clone();
    assert_eq!(CLONED.load(Ordering::SeqCst), 0);

    // Cloning the liar directly still runs its own impl.
    let _ = Liar.clone();
    assert_eq!(CLONED.load(Ordering::SeqCst), 1);
}
