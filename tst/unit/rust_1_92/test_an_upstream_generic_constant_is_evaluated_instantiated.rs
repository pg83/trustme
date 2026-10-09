//@ aux-build: generic_assoc_const.rs
// `Complex::<f32>::ZERO` is `Self::new(T::ZERO, T::ZERO)` in a generic impl
// of another crate, whose MIR comes from that crate's metadata. rustc's const
// evaluator runs the instance `Complex::<f32>::ZERO`: every type in the body,
// a constant operand's included, is instantiated with the instance's
// arguments (`instantiate_from_current_frame_and_normalize_erasing_regions`).
// The argument `T::ZERO` was laid out as the bare `T` and the evaluation
// aborted. pulp, under image, reads num-complex's `Complex::<f32>::ZERO` this
// way.
use generic_assoc_const::Complex;

static ORIGIN: Complex<f32> = Complex::<f32>::ZERO;

fn main() {
    assert_eq!(ORIGIN, Complex::new(0.0, 0.0));
}
