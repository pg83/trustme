// `let r: Holder<&i32> = make(t)` with `t: &mut i32`: upstream derives the
// expected parameter types from the expected result (`check_argument_types`
// relates the declared result to the expectation under a probe), so the argument
// is coerced into `&i32` - a reborrow - and `X` is then `&i32`, rather than `X`
// being bound to `&mut i32` by the argument first and the result mismatching.
struct Holder<X>(X);

fn make<X>(x: X) -> Holder<X> {
    Holder(x)
}

fn main() {
    let mut n = 5;
    let t: &mut i32 = &mut n;
    let r: Holder<&i32> = make(t);
    assert_eq!(*r.0, 5);
}
