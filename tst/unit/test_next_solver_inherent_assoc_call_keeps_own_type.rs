//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// An inherent associated call nested in a trait method owns its type
// parameter.  The current trait's `Self::X` must not leak into the result of
// `Uniform::new_inclusive` while the `?` desugaring is still being inferred.
trait SampleBorrow<X> {
    fn borrow(&self) -> &X;
}

impl<X: SampleUniform> SampleBorrow<X> for X {
    fn borrow(&self) -> &X {
        self
    }
}

impl<X: SampleUniform> SampleBorrow<X> for &X {
    fn borrow(&self) -> &X {
        self
    }
}

struct Uniform<X: SampleUniform>(X::Sampler);

enum Error {
    EmptyRange,
}

impl<X: SampleUniform> Uniform<X> {
    fn new_inclusive<B1, B2>(low: B1, high: B2) -> Result<Uniform<X>, Error>
    where
        B1: SampleBorrow<X> + Sized,
        B2: SampleBorrow<X> + Sized,
    {
        X::Sampler::new_inclusive(low, high).map(Uniform)
    }
}

enum Mode {
    Small { secs: u64, nanos: Uniform<u32> },
    Medium { nanos: Uniform<u64> },
    Large {
        max_secs: u64,
        max_nanos: u32,
        secs: Uniform<u64>,
    },
}

#[derive(Clone, Copy, PartialEq, PartialOrd)]
struct Duration;
struct UniformDuration;
struct UniformU32;
struct UniformU64;

impl Duration {
    fn as_secs(&self) -> u64 {
        1
    }

    fn subsec_nanos(&self) -> u32 {
        1
    }
}

trait SampleUniform: Sized {
    type Sampler: UniformSampler<X = Self>;
}

trait UniformSampler: Sized {
    type X;

    fn new<B1, B2>(low: B1, high: B2) -> Result<Self, Error>
    where
        B1: SampleBorrow<Self::X> + Sized,
        B2: SampleBorrow<Self::X> + Sized,
    {
        Self::new_inclusive(low, high)
    }

    fn new_inclusive<B1, B2>(low: B1, high: B2) -> Result<Self, Error>
    where
        B1: SampleBorrow<Self::X> + Sized,
        B2: SampleBorrow<Self::X> + Sized;
}

impl SampleUniform for u32 {
    type Sampler = UniformU32;
}

impl SampleUniform for u64 {
    type Sampler = UniformU64;
}

impl UniformSampler for UniformU64 {
    type X = u64;

    fn new_inclusive<B1, B2>(_low: B1, _high: B2) -> Result<Self, Error>
    where
        B1: SampleBorrow<Self::X>,
        B2: SampleBorrow<Self::X>,
    {
        Ok(UniformU64)
    }
}

impl UniformSampler for UniformU32 {
    type X = u32;

    fn new_inclusive<B1, B2>(_low: B1, _high: B2) -> Result<Self, Error>
    where
        B1: SampleBorrow<Self::X>,
        B2: SampleBorrow<Self::X>,
    {
        Ok(UniformU32)
    }
}

impl SampleUniform for Duration {
    type Sampler = UniformDuration;
}

impl UniformSampler for UniformDuration {
    type X = Duration;

    fn new_inclusive<B1, B2>(low_b: B1, high_b: B2) -> Result<Self, Error>
    where
        B1: SampleBorrow<Self::X>,
        B2: SampleBorrow<Self::X>,
    {
        let low = *low_b.borrow();
        let high = *high_b.borrow();
        if !(low <= high) {
            return Err(Error::EmptyRange);
        }

        let low_s = low.as_secs();
        let low_n = low.subsec_nanos();
        let mut high_s = high.as_secs();
        let mut high_n = high.subsec_nanos();

        if high_n < low_n {
            high_s -= 1;
            high_n += 1_000_000_000;
        }

        let _mode = if low_s == high_s {
            Mode::Small {
                secs: low_s,
                nanos: Uniform::new_inclusive(low_n, high_n)?,
            }
        } else {
            let max = high_s
                .checked_mul(1_000_000_000)
                .and_then(|n| n.checked_add(u64::from(high_n)));

            if let Some(higher_bound) = max {
                let lower_bound = low_s * 1_000_000_000 + u64::from(low_n);
                Mode::Medium {
                    nanos: Uniform::new_inclusive(lower_bound, higher_bound)?,
                }
            } else {
                let max_nanos = high_n - low_n;
                Mode::Large {
                    max_secs: high_s,
                    max_nanos,
                    secs: Uniform::new_inclusive(low_s, high_s)?,
                }
            }
        };
        Ok(UniformDuration)
    }
}
