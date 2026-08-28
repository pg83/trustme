//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// The receiver's declared associated-type bound is a known method source.
// A merely ambiguous possibility for the trait currently being implemented
// must not shadow it just because both traits use the same method name.
trait Distribution<X> {
    fn sample(&self) -> X;
}

struct Uniform<X: SampleUniform>(X::Sampler);

impl<X: SampleUniform> Distribution<X> for Uniform<X> {
    fn sample(&self) -> X {
        self.0.sample()
    }
}

trait SampleUniform: Sized {
    type Sampler: UniformSampler<X = Self>;
}

trait UniformSampler: Sized {
    type X;
    fn sample(&self) -> Self::X;
}
