#![feature(const_precise_live_drops)]

use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct Token(u8);

impl Drop for Token {
    fn drop(&mut self) {
        DROPS.fetch_add(self.0 as usize, Ordering::SeqCst);
    }
}

struct Slots<const N: usize>([Option<Token>; N]);

const EMPTY: usize = {
    let _slots = Slots::<0>([]);
    13
};

const VACANT: usize = {
    let _slots = Slots([None, None, None]);
    29
};

fn main() {
    assert_eq!((EMPTY, VACANT), (13, 29));
    assert_eq!(DROPS.load(Ordering::SeqCst), 0);
    {
        let _slots = Slots([None, Some(Token(7)), Some(Token(9))]);
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 16);
}
