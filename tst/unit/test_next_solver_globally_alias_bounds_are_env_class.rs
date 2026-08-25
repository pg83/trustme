//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// Alias-bound candidates (bounds on the associated type declaration,
// including supertrait-elaborated ones like IntT: CastFrom<u16>) are the
// same preference class as ParamEnv predicates.  The non-global-ParamEnv
// shadow must not drop them, or the directly-declared CastFrom<Self::Int>
// commits and guides the argument to the wrong type.  Mirrors libm
// generic/sqrt.rs (RSQRT_TAB cast_from).

trait CastFrom<T> {
    fn cast_from(v: T) -> Self;
}

trait IntT: CastFrom<u16> + CastFrom<u32> {}

trait Float2 {
    type Int;
    type ISet1: IntT + CastFrom<Self::Int>;
}

static TAB: [u16; 2] = [1, 2];

fn f<F: Float2>(i: usize) -> F::ISet1 {
    F::ISet1::cast_from(TAB[i])
}
