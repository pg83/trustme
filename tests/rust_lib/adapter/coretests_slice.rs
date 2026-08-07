extern crate self as rand;

pub trait RngCore {
    fn next_u32(&mut self) -> u32;
}

pub trait Random {
    fn random<R: RngCore + ?Sized>(rng: &mut R) -> Self;
}

impl Random for i32 {
    fn random<R: RngCore + ?Sized>(rng: &mut R) -> Self {
        rng.next_u32() as i32
    }
}

pub trait Rng: RngCore {
    fn random<T: Random>(&mut self) -> T {
        T::random(self)
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

pub mod seq {
    use super::RngCore;

    pub trait IndexedRandom<T> {
        fn choose<'a, R: RngCore + ?Sized>(&'a self, rng: &mut R) -> Option<&'a T>;
    }

    impl<T> IndexedRandom<T> for [T] {
        fn choose<'a, R: RngCore + ?Sized>(&'a self, rng: &mut R) -> Option<&'a T> {
            if self.is_empty() {
                None
            } else {
                Some(&self[rng.next_u32() as usize % self.len()])
            }
        }
    }
}

pub(crate) fn test_rng() -> XorShiftRng {
    XorShiftRng::from_seed([0x42; 16])
}
