// rand's `test_update_weights_errors` loops `for (weights, update, err) in
// data.iter()` and passes `update: &&[(i32, &i32)]` to `update_weights(&mut
// self, &[(usize, &X)])`. rustc checks a pattern before any use of its
// bindings, so `update` is `&&[..]` when the argument is coerced (deref
// coercion). Our `for` pattern waits for the element type of `data`, whose
// `[..]` indexings resolve one per pass; meanwhile the argument-binding phase
// gave the still-open `update` the parameter's type and the pattern then
// failed to match. A pattern binding's own variable is now left to its pattern.
struct Weighted<X> {
    weights: Vec<X>,
    total: X,
}

impl<X> Weighted<X>
where
    X: for<'a> core::ops::AddAssign<&'a X> + Clone + Default + PartialOrd,
{
    fn new(weights: Vec<X>) -> Result<Self, ()> {
        let mut total = X::default();
        for w in &weights {
            total += w;
        }
        Ok(Weighted { weights, total })
    }

    fn update_weights(&mut self, new_weights: &[(usize, &X)]) -> Result<(), u8> {
        for (i, w) in new_weights {
            if *i >= self.weights.len() {
                return Err(1);
            }
            self.weights[*i] = (*w).clone();
        }
        Ok(())
    }
}

fn main() {
    let data = [
        (&[1i32, 0, 0][..], &[(0, &0)][..], 2u8),
        (&[1][..], &[(1, &1)][..], 1u8),
    ];
    for (weights, update, err) in data.iter() {
        let total_weight = weights.iter().sum::<i32>();
        let mut distr = Weighted::new(weights.to_vec()).unwrap();
        assert_eq!(distr.total, total_weight);
        match distr.update_weights(update) {
            Ok(_) => assert_eq!(*err, 2),
            Err(e) => assert_eq!(e, *err),
        }
    }
}
