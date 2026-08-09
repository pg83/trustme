//@ rust-lib-dev-dependencies

use rand::distr::{Distribution, Uniform};
use rand::seq::SliceRandom;
use rand::SeedableRng;
use rand_xorshift::XorShiftRng;

fn main() {
    let mut rng = XorShiftRng::seed_from_u64(0x5eed);
    let distribution = Uniform::new(10u32, 20).unwrap();
    let value = distribution.sample(&mut rng);
    let mut choices = [1u32, 2, 3, 4];
    choices.shuffle(&mut rng);

    assert!((10..20).contains(&value));
    assert_eq!(choices.iter().sum::<u32>(), 10);
}
