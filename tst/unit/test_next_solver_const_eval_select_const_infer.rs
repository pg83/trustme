//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(core_intrinsics, portable_simd)]

use std::simd::{LaneCount, Simd, SimdElement, SupportedLaneCount, Swizzle};

const fn splat<T, const N: usize>(value: T) -> Simd<T, N>
where
    T: SimdElement,
    LaneCount<N>: SupportedLaneCount,
{
    const fn splat_const<T, const N: usize>(value: T) -> Simd<T, N>
    where
        T: SimdElement,
        LaneCount<N>: SupportedLaneCount,
    {
        Simd::from_array([value; N])
    }

    fn splat_rt<T, const N: usize>(value: T) -> Simd<T, N>
    where
        T: SimdElement,
        LaneCount<N>: SupportedLaneCount,
    {
        struct Splat;
        impl<const N: usize> Swizzle<N> for Splat {
            const INDEX: [usize; N] = [0; N];
        }

        Splat::swizzle::<T, 1>(Simd::<T, 1>::from([value]))
    }

    core::intrinsics::const_eval_select((value,), splat_const, splat_rt)
}

fn main() {
    assert_eq!(splat::<u8, 4>(7).to_array(), [7; 4]);
}
