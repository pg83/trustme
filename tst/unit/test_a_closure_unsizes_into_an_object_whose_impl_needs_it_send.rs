//@ run-pass
// schemars pushes `Box::new(|s: &mut Schema| ..)` into a
// `Vec<Box<dyn GenTransform>>`, and `GenTransform` holds of a closure only
// when it is `Send`. Whether a closure is `Send` waits for its captures,
// known at the end of type checking. rustc selects `closure: Unsize<dyn Tr>`
// by its builtin candidate and keeps every predicate of the object -
// `closure: Tr` as much as a marker - as an obligation of the coercion
// (`confirm_builtin_unsize_candidate`); an ambiguous `closure: Tr` left the
// coercion undecided, and it was equated at the end.
trait Tr {
    fn go(&mut self, x: &mut u32);
}

impl<F: FnMut(&mut u32) + Send> Tr for F {
    fn go(&mut self, x: &mut u32) {
        self(x)
    }
}

fn main() {
    let step = 2u32;
    let mut transforms: Vec<Box<dyn Tr>> = Vec::new();
    transforms.insert(0, Box::new(|s: &mut u32| *s += 1));
    transforms.push(Box::new(move |s: &mut u32| *s += step));
    let mut x = 0;
    for t in &mut transforms {
        t.go(&mut x);
    }
    assert_eq!(x, 3);
}
