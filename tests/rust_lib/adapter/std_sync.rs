#![feature(lazy_get)]
#![feature(lock_value_accessors)]
#![feature(macro_metavar_expr_concat)]
#![feature(mapped_lock_guards)]
#![feature(mpmc_channel)]
#![feature(nonpoison_mutex)]
#![feature(once_cell_try)]
#![feature(reentrant_lock)]
#![feature(rwlock_downgrade)]
#![feature(std_internals)]
#![feature(sync_nonpoison)]
#![allow(internal_features)]

extern crate self as rand;
extern crate self as rand_xorshift;

pub trait RngCore {
    fn next_u32(&mut self) -> u32;
}

pub trait Rng: RngCore {
    fn random_bool(&mut self, probability: f64) -> bool {
        assert!((0.0..=1.0).contains(&probability));
        (self.next_u32() as f64) < probability * (u32::MAX as f64 + 1.0)
    }
}

impl<T: RngCore + ?Sized> Rng for T {}

pub trait SeedableRng: Sized {
    type Seed;

    fn from_seed(seed: Self::Seed) -> Self;
}

pub struct XorShiftRng {
    state: [u32; 4],
}

impl SeedableRng for XorShiftRng {
    type Seed = [u8; 16];

    fn from_seed(seed: Self::Seed) -> Self {
        let mut state = [0; 4];
        for (index, word) in state.iter_mut().enumerate() {
            let offset = index * 4;
            *word = u32::from_le_bytes(seed[offset..offset + 4].try_into().unwrap());
        }
        if state == [0; 4] {
            state[0] = 1;
        }
        Self { state }
    }
}

impl RngCore for XorShiftRng {
    fn next_u32(&mut self) -> u32 {
        let value = self.state[0] ^ (self.state[0] << 11);
        self.state.rotate_left(1);
        self.state[3] ^= self.state[3] >> 19 ^ value ^ (value >> 8);
        self.state[3]
    }
}

#[path = "../upstream/std/tests/sync/barrier.rs"]
mod barrier;
#[path = "../upstream/std/tests/sync/condvar.rs"]
mod condvar;
#[path = "../upstream/std/tests/sync/lazy_lock.rs"]
mod lazy_lock;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/mpmc.rs"]
mod mpmc;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/mpsc.rs"]
mod mpsc;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/mpsc_sync.rs"]
mod mpsc_sync;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/mutex.rs"]
mod mutex;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/once.rs"]
mod once;
#[path = "../upstream/std/tests/sync/once_lock.rs"]
mod once_lock;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/reentrant_lock.rs"]
mod reentrant_lock;
#[cfg(not(any(target_os = "emscripten", target_os = "wasi")))]
#[path = "../upstream/std/tests/sync/rwlock.rs"]
mod rwlock;

#[path = "../upstream/std/tests/common/mod.rs"]
mod common;

#[track_caller]
fn result_unwrap<T, E: std::fmt::Debug>(value: Result<T, E>) -> T {
    value.unwrap()
}

macro_rules! nonpoison_and_poison_unwrap_test {
    (
        name: $name:ident,
        test_body: {$($test_body:tt)*}
    ) => {
        #[test]
        fn ${concat(nonpoison_, $name)}() {
            #[allow(unused_imports)]
            use ::std::convert::identity as maybe_unwrap;
            use ::std::sync::nonpoison as locks;

            $($test_body)*
        }

        #[test]
        fn ${concat(poison_, $name)}() {
            #[allow(unused_imports)]
            use super::result_unwrap as maybe_unwrap;
            use ::std::sync::poison as locks;

            $($test_body)*
        }
    }
}

use nonpoison_and_poison_unwrap_test;
